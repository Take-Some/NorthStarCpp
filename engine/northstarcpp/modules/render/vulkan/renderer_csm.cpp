// Vulkan directional cascaded shadow depth storage and stable camera fitting.
#include "renderer_private.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
namespace northstarcpp::render::vulkan {
namespace {
void csm_check(VkResult code,const char* name) {
    if(code!=VK_SUCCESS)throw std::runtime_error(
        std::string(name)+" VkResult="+std::to_string(int(code)));
}
struct Vec3 {double x,y,z;};
Vec3 operator+(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 operator-(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 operator*(Vec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 cross(Vec3 a,Vec3 b){
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
Vec3 unit(Vec3 v){return v*(1./std::sqrt(std::max(1.e-20,dot(v,v))));}
}
void Renderer::destroy_csm_resources() noexcept {
    if(!device_)return;
    if(csm_primitive_pipeline_)
        vkDestroyPipeline(device_,csm_primitive_pipeline_,nullptr);
    if(csm_material_pipeline_)
        vkDestroyPipeline(device_,csm_material_pipeline_,nullptr);
    if(csm_pipeline_layout_)
        vkDestroyPipelineLayout(device_,csm_pipeline_layout_,nullptr);
    if(csm_descriptor_pool_)
        vkDestroyDescriptorPool(device_,csm_descriptor_pool_,nullptr);
    if(csm_descriptor_layout_)
        vkDestroyDescriptorSetLayout(device_,csm_descriptor_layout_,nullptr);
    if(csm_buffer_)vkDestroyBuffer(device_,csm_buffer_,nullptr);
    if(csm_buffer_memory_)vkFreeMemory(device_,csm_buffer_memory_,nullptr);
    if(csm_sampler_)vkDestroySampler(device_,csm_sampler_,nullptr);
    for(auto& view:csm_layer_views_)
        if(view)vkDestroyImageView(device_,view,nullptr);
    if(csm_array_view_)vkDestroyImageView(device_,csm_array_view_,nullptr);
    if(csm_image_)vkDestroyImage(device_,csm_image_,nullptr);
    if(csm_image_memory_)vkFreeMemory(device_,csm_image_memory_,nullptr);
    csm_primitive_pipeline_=csm_material_pipeline_=VK_NULL_HANDLE;
    csm_pipeline_layout_=VK_NULL_HANDLE;
    csm_descriptor_pool_=VK_NULL_HANDLE;
    csm_descriptor_layout_=VK_NULL_HANDLE;
    csm_descriptor_set_=VK_NULL_HANDLE;
    csm_buffer_=VK_NULL_HANDLE;csm_buffer_memory_=VK_NULL_HANDLE;
    csm_sampler_=VK_NULL_HANDLE;
    csm_layer_views_={};csm_array_view_=VK_NULL_HANDLE;
    csm_image_=VK_NULL_HANDLE;csm_image_memory_=VK_NULL_HANDLE;
}
void Renderer::create_csm_resources() {
    if(!csm_enabled_)return;
    constexpr VkFormat format=VK_FORMAT_D32_SFLOAT;
    VkFormatProperties supported{};
    vkGetPhysicalDeviceFormatProperties(physical_,format,&supported);
    constexpr auto needed=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|
                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if((supported.optimalTilingFeatures&needed)!=needed)
        throw std::runtime_error("Vulkan CSM requires sampled D32");
    VkImageCreateInfo desc{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    desc.imageType=VK_IMAGE_TYPE_2D;
    desc.extent={csm_resolution_,csm_resolution_,1};
    desc.mipLevels=1;desc.arrayLayers=csm_cascades_;
    desc.format=format;desc.samples=VK_SAMPLE_COUNT_1_BIT;
    desc.tiling=VK_IMAGE_TILING_OPTIMAL;
    desc.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|
               VK_IMAGE_USAGE_SAMPLED_BIT;
    csm_check(vkCreateImage(device_,&desc,nullptr,&csm_image_),
              "vkCreateImage(CSM)");
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_,csm_image_,&req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize=req.size;
    ai.memoryTypeIndex=find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    csm_check(vkAllocateMemory(device_,&ai,nullptr,&csm_image_memory_),
              "vkAllocateMemory(CSM)");
    csm_check(vkBindImageMemory(device_,csm_image_,csm_image_memory_,0),
              "vkBindImageMemory(CSM)");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image=csm_image_;view.format=format;
    view.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    view.subresourceRange.levelCount=1;
    view.subresourceRange.layerCount=csm_cascades_;
    csm_check(vkCreateImageView(device_,&view,nullptr,&csm_array_view_),
              "vkCreateImageView(CSM array)");
    view.viewType=VK_IMAGE_VIEW_TYPE_2D;
    view.subresourceRange.layerCount=1;
    for(unsigned i=0;i<csm_cascades_;++i) {
        view.subresourceRange.baseArrayLayer=i;
        csm_check(vkCreateImageView(device_,&view,nullptr,&csm_layer_views_[i]),
                  "vkCreateImageView(CSM layer)");
    }
    VkSamplerCreateInfo samp{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samp.magFilter=samp.minFilter=VK_FILTER_NEAREST;
    samp.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samp.addressModeU=samp.addressModeV=samp.addressModeW=
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    csm_check(vkCreateSampler(device_,&samp,nullptr,&csm_sampler_),
              "vkCreateSampler(CSM)");
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size=sizeof(csm_uniform_);
    buffer.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    csm_check(vkCreateBuffer(device_,&buffer,nullptr,&csm_buffer_),
              "vkCreateBuffer(CSM uniforms)");
    VkMemoryRequirements br{};
    vkGetBufferMemoryRequirements(device_,csm_buffer_,&br);
    VkMemoryAllocateInfo bm{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    bm.allocationSize=br.size;
    bm.memoryTypeIndex=find_memory_type(br.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    csm_check(vkAllocateMemory(device_,&bm,nullptr,&csm_buffer_memory_),
              "vkAllocateMemory(CSM uniforms)");
    csm_check(vkBindBufferMemory(device_,csm_buffer_,csm_buffer_memory_,0),
              "vkBindBufferMemory(CSM uniforms)");
    VkDescriptorSetLayoutBinding binding{};
    binding.binding=0;binding.descriptorCount=1;
    binding.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.stageFlags=VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo layout{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount=1;layout.pBindings=&binding;
    csm_check(vkCreateDescriptorSetLayout(device_,&layout,nullptr,
              &csm_descriptor_layout_),"vkCreateDescriptorSetLayout(CSM)");
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&pool_size;
    csm_check(vkCreateDescriptorPool(device_,&pool,nullptr,
              &csm_descriptor_pool_),"vkCreateDescriptorPool(CSM)");
    VkDescriptorSetAllocateInfo allocate{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool=csm_descriptor_pool_;
    allocate.descriptorSetCount=1;
    allocate.pSetLayouts=&csm_descriptor_layout_;
    csm_check(vkAllocateDescriptorSets(device_,&allocate,
              &csm_descriptor_set_),"vkAllocateDescriptorSets(CSM)");
    VkDescriptorBufferInfo info{csm_buffer_,0,sizeof(csm_uniform_)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet=csm_descriptor_set_;write.dstBinding=0;
    write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount=1;write.pBufferInfo=&info;
    vkUpdateDescriptorSets(device_,1,&write,0,nullptr);
}
void Renderer::update_csm_matrices() {
    if(!csm_enabled_)return;
    const double near_clip=std::max(.01,double(pick_camera_.camera_near_clip));
    const double far_clip=std::min(180.,std::max(near_clip+1.,
                     double(pick_camera_.camera_far_clip)));
    const double fov=pick_camera_.camera_fov_y_degrees>1.f?
        pick_camera_.camera_fov_y_degrees:70.;
    const double tangent=std::tan(fov*3.141592653589793/360.);
    const double aspect=double(extent_.width)/std::max(1u,extent_.height);
    const double yaw=pick_camera_.camera_yaw,pitch=pick_camera_.camera_pitch;
    Vec3 eye{pick_camera_.camera_x,pick_camera_.camera_y,pick_camera_.camera_z};
    Vec3 forward{std::sin(yaw)*std::cos(pitch),std::sin(pitch),
                 std::cos(yaw)*std::cos(pitch)};
    Vec3 right{std::cos(yaw),0,-std::sin(yaw)};
    Vec3 up=unit(cross(forward,right));
    const Vec3 sun=unit({.45,.82,.35});
    const Vec3 light_right=unit(cross({0,1,0},sun));
    const Vec3 light_up=unit(cross(sun,light_right));
    double start=near_clip;
    for(unsigned c=0;c<csm_cascades_;++c) {
        const double t=double(c+1)/csm_cascades_;
        const double finish=.70*near_clip*std::pow(far_clip/near_clip,t)+
                            .30*(near_clip+(far_clip-near_clip)*t);
        csm_uniform_.splits[c]=float(finish);
        Vec3 center{0,0,0};
        std::array<Vec3,8> corners{};
        unsigned index=0;
        for(double d:{start,finish})for(int y:{-1,1})for(int x:{-1,1}) {
            Vec3 point=eye+forward*d+
                       right*(double(x)*d*tangent*aspect)+
                       up*(double(y)*d*tangent);
            corners[index++]=point;center=center+point;
        }
        center=center*(1./8.);
        double radius=0;
        for(auto p:corners)radius=std::max(radius,std::sqrt(dot(p-center,p-center)));
        radius=std::ceil((radius*1.02+.25)*16.)/16.;
        const double texel=2.*radius/csm_resolution_;
        double cx=std::round(dot(center,light_right)/texel)*texel;
        double cy=std::round(dot(center,light_up)/texel)*texel;
        const double margin=std::max(20.,radius*.25);
        const double depth_start=dot(center,sun)+radius+margin;
        const double range=2.*(radius+margin);
        auto& m=csm_uniform_.matrices[c];
        std::fill(std::begin(m),std::end(m),0.f);
        // Row-vector matrix stored verbatim; GLSL column-major reads its
        // transpose, yielding the same clip-space transformation.
        m[0]=float(light_right.x/radius);
        m[4]=float(light_right.y/radius);
        m[8]=float(light_right.z/radius);
        m[12]=float(-cx/radius);
        m[1]=float(light_up.x/radius);
        m[5]=float(light_up.y/radius);
        m[9]=float(light_up.z/radius);
        m[13]=float(-cy/radius);
        m[2]=float(-sun.x/range);
        m[6]=float(-sun.y/range);
        m[10]=float(-sun.z/range);
        m[14]=float(depth_start/range);
        m[15]=1.f;
        start=finish;
    }
    csm_uniform_.splits[3]=.10f;
    void* mapped=nullptr;
    csm_check(vkMapMemory(device_,csm_buffer_memory_,0,
              sizeof(csm_uniform_),0,&mapped),"vkMapMemory(CSM)");
    std::memcpy(mapped,&csm_uniform_,sizeof(csm_uniform_));
    vkUnmapMemory(device_,csm_buffer_memory_);
}
} // namespace northstarcpp::render::vulkan
