// Native Vulkan GPU skinned PBR: original eight YDD influences per vertex.
// GPU compute writes the same MaterialClipVertex layout consumed by the
// existing material PSO. No host-side bone deformation or OBJ expansion.
#include "renderer_private.hpp"
#include "material_spirv.hpp"
#include "material_graph_glsl.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace northstarcpp::render::vulkan {
namespace {
void check(VkResult result,const char* name) {
    if(result!=VK_SUCCESS)
        throw std::runtime_error(std::string(name)+" failed: VkResult="+
                                 std::to_string(static_cast<int>(result)));
}
constexpr VkDeviceSize max_skin_bytes=
    NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES*sizeof(NorthStarSkinMaterialVertex);
constexpr VkDeviceSize max_bone_bytes=
    NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES*
    NORTHSTARCPP_SKIN_MAX_JOINTS*16u*sizeof(float);
constexpr std::uint32_t max_skin_vertices=
    NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES;
}
void Renderer::set_skinned_material_batches(
    const NorthStarSkinnedMaterialBatch* batches,std::uint32_t count)
{
    if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES || (count&&!batches))
        throw std::invalid_argument("Vulkan GPU skin batch count/pointer invalid");
    std::vector<SkinSource> next;
    next.reserve(count);
    std::uint32_t total_vertices=0;
    std::uint64_t total_textures=0;
    for(std::uint32_t i=0;i<count;++i) {
        const auto& input=batches[i];
        const auto& policy=input.material;
        const auto& material=policy.material;
        if(policy.graph_hlsl_bytes || policy.blend_mode!=0 ||
           !std::isfinite(policy.opacity) || policy.opacity!=1.f)
            throw std::invalid_argument(
                "Vulkan native GPU skin v1 requires opaque, default PBR");
        if(!input.vertices || !input.palette || !input.vertex_count ||
           input.vertex_count%3 || input.joint_count==0 ||
           input.joint_count>NORTHSTARCPP_SKIN_MAX_JOINTS ||
           input.vertex_count>max_skin_vertices-total_vertices)
            throw std::invalid_argument("Vulkan native GPU skin topology invalid");
        SkinSource source;
        source.vertices.assign(input.vertices,input.vertices+input.vertex_count);
        source.palette.assign(input.palette,
            input.palette+std::size_t(input.joint_count)*16u);
        for(float f:source.palette)
            if(!std::isfinite(f)||std::abs(f)>1e6f)
                throw std::invalid_argument("Invalid GPU bone palette");
        for(auto const& vertex:source.vertices) {
            for(float value:{vertex.x,vertex.y,vertex.z,
                             vertex.nx,vertex.ny,vertex.nz,vertex.u,vertex.v})
                if(!std::isfinite(value)||std::abs(value)>1e6f)
                    throw std::invalid_argument("Invalid native skin vertex");
            float weights=0.f;
            for(unsigned influence=0;influence<8;++influence) {
                auto w=vertex.weights[influence];
                if(!std::isfinite(w)||w<0.f||w>1.f ||
                   (w>0.f&&vertex.joints[influence]>=input.joint_count))
                    throw std::invalid_argument("Invalid GPU bone influence");
                weights+=w;
            }
            if(std::abs(weights-1.f)>.01f)
                throw std::invalid_argument("GPU skin weights must sum to one");
        }
        auto& output=source.material;
        output.roughness=material.roughness;
        output.metallic=material.metallic;
        output.unlit=material.unlit;
        if(!std::isfinite(output.roughness)||output.roughness<0.f||
           output.roughness>1.f||!std::isfinite(output.metallic)||
           output.metallic<0.f||output.metallic>1.f||output.unlit>1)
            throw std::invalid_argument("Invalid GPU skin PBR parameters");
        for(unsigned k=0;k<4;++k) {
            auto value=material.base_color[k];
            if(!std::isfinite(value)||value<0.f||value>1.f)
                throw std::invalid_argument("Invalid GPU skin material tint");
            output.tint[k]=value;
        }
        for(unsigned slot=0;slot<5;++slot) {
            auto const& image=material.textures[slot];
            if(!image.rgba8) {
                if(image.width||image.height||image.data_bytes)
                    throw std::invalid_argument("Malformed GPU skin texture slot");
                continue;
            }
            if(!image.width||!image.height||image.width>4096||
               image.height>4096||std::uint64_t(image.width)*image.height*4!=
               image.data_bytes)
                throw std::invalid_argument("Invalid GPU skin material texture");
            total_textures+=image.data_bytes;
            if(total_textures>256u*1024u*1024u)
                throw std::invalid_argument("GPU skin material texture budget exceeded");
            output.widths[slot]=image.width;output.heights[slot]=image.height;
            output.textures[slot].assign(image.rgba8,image.rgba8+image.data_bytes);
        }
        total_vertices+=input.vertex_count;
        next.push_back(std::move(source));
    }
    bool textures_same=next.size()==skinned_sources_.size();
    if(textures_same)for(std::size_t i=0;i<next.size()&&textures_same;++i)
        for(unsigned slot=0;slot<5;++slot)
            if(next[i].material.textures[slot]!=skinned_sources_[i].material.textures[slot]||
               next[i].material.widths[slot]!=skinned_sources_[i].material.widths[slot]||
               next[i].material.heights[slot]!=skinned_sources_[i].material.heights[slot])
                textures_same=false;
    if(count&&!skin_pipeline_)create_skin_resources();
    skinned_sources_=std::move(next);
    if(!textures_same)material_textures_dirty_=true;
}
void Renderer::create_skin_resources() {
    if(skin_pipeline_)return;
    auto create_buffer=[&](VkDeviceSize size,VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags memory_flags,
                           VkBuffer& buffer,VkDeviceMemory& memory) {
        VkBufferCreateInfo desc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        desc.size=size;desc.usage=usage;
        check(vkCreateBuffer(device_,&desc,nullptr,&buffer),
              "vkCreateBuffer(GPU skin)");
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_,buffer,&req);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize=req.size;
        allocation.memoryTypeIndex=find_memory_type(req.memoryTypeBits,memory_flags);
        check(vkAllocateMemory(device_,&allocation,nullptr,&memory),
              "vkAllocateMemory(GPU skin)");
        check(vkBindBufferMemory(device_,buffer,memory,0),
              "vkBindBufferMemory(GPU skin)");
    };
    try {
        create_buffer(max_skin_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            skin_input_,skin_input_memory_);
        create_buffer(max_skin_bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            skin_output_,skin_output_memory_);
        create_buffer(max_bone_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            skin_palette_,skin_palette_memory_);
        std::array<VkDescriptorSetLayoutBinding,3> bindings{};
        for(unsigned i=0;i<bindings.size();++i) {
            bindings[i].binding=i;
            bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount=1;
            bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layout{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount=static_cast<std::uint32_t>(bindings.size());
        layout.pBindings=bindings.data();
        check(vkCreateDescriptorSetLayout(device_,&layout,nullptr,&skin_set_layout_),
              "vkCreateDescriptorSetLayout(GPU skin)");
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3};
        VkDescriptorPoolCreateInfo pool{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&pool_size;
        check(vkCreateDescriptorPool(device_,&pool,nullptr,&skin_descriptor_pool_),
              "vkCreateDescriptorPool(GPU skin)");
        VkDescriptorSetAllocateInfo allocate{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool=skin_descriptor_pool_;
        allocate.descriptorSetCount=1;
        allocate.pSetLayouts=&skin_set_layout_;
        check(vkAllocateDescriptorSets(device_,&allocate,&skin_set_),
              "vkAllocateDescriptorSets(GPU skin)");
        VkDescriptorBufferInfo info[3]{
            {skin_input_,0,max_skin_bytes},
            {skin_output_,0,max_skin_bytes},
            {skin_palette_,0,max_bone_bytes}
        };
        VkWriteDescriptorSet writes[3]{};
        for(unsigned i=0;i<3;++i) {
            writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet=skin_set_;
            writes[i].dstBinding=i;
            writes[i].descriptorCount=1;
            writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo=&info[i];
        }
        vkUpdateDescriptorSets(device_,3,writes,0,nullptr);
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize=sizeof(shaders::material_skin_compute);
        shader.pCode=shaders::material_skin_compute;
        check(vkCreateShaderModule(device_,&shader,nullptr,&skin_shader_),
              "vkCreateShaderModule(GPU skin compute)");
        VkPushConstantRange push{};
        push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        push.size=sizeof(SkinComputePush);
        VkPipelineLayoutCreateInfo pipeline_layout{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeline_layout.setLayoutCount=1;
        pipeline_layout.pSetLayouts=&skin_set_layout_;
        pipeline_layout.pushConstantRangeCount=1;
        pipeline_layout.pPushConstantRanges=&push;
        check(vkCreatePipelineLayout(device_,&pipeline_layout,nullptr,
                                     &skin_pipeline_layout_),
              "vkCreatePipelineLayout(GPU skin)");
        VkComputePipelineCreateInfo compute{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compute.layout=skin_pipeline_layout_;
        compute.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        compute.stage.module=skin_shader_;
        compute.stage.pName="main";
        check(vkCreateComputePipelines(device_,VK_NULL_HANDLE,1,&compute,nullptr,
                                       &skin_pipeline_),
              "vkCreateComputePipelines(GPU skin)");
    }catch(...) {destroy_skin_resources();throw;}
}
void Renderer::destroy_skin_resources() noexcept {
    if(!device_)return;
    if(skin_pipeline_)vkDestroyPipeline(device_,skin_pipeline_,nullptr);
    if(skin_pipeline_layout_)
        vkDestroyPipelineLayout(device_,skin_pipeline_layout_,nullptr);
    if(skin_shader_)vkDestroyShaderModule(device_,skin_shader_,nullptr);
    if(skin_descriptor_pool_)
        vkDestroyDescriptorPool(device_,skin_descriptor_pool_,nullptr);
    if(skin_set_layout_)
        vkDestroyDescriptorSetLayout(device_,skin_set_layout_,nullptr);
    if(skin_input_)vkDestroyBuffer(device_,skin_input_,nullptr);
    if(skin_output_)vkDestroyBuffer(device_,skin_output_,nullptr);
    if(skin_palette_)vkDestroyBuffer(device_,skin_palette_,nullptr);
    if(skin_input_memory_)vkFreeMemory(device_,skin_input_memory_,nullptr);
    if(skin_output_memory_)vkFreeMemory(device_,skin_output_memory_,nullptr);
    if(skin_palette_memory_)vkFreeMemory(device_,skin_palette_memory_,nullptr);
    skin_pipeline_=VK_NULL_HANDLE;skin_pipeline_layout_=VK_NULL_HANDLE;
    skin_shader_=VK_NULL_HANDLE;skin_descriptor_pool_=VK_NULL_HANDLE;
    skin_set_layout_=VK_NULL_HANDLE;skin_set_=VK_NULL_HANDLE;
    skin_input_=skin_output_=skin_palette_=VK_NULL_HANDLE;
    skin_input_memory_=skin_output_memory_=skin_palette_memory_=VK_NULL_HANDLE;
    skinned_sources_.clear();skin_draws_.clear();
}
void Renderer::upload_skin_geometry() {
    skin_draws_.clear();
    if(skinned_sources_.empty())return;
    if(!skin_pipeline_)throw std::logic_error("Vulkan GPU skin is uninitialized");
    std::uint64_t vertex_count=0,palette_count=0;
    for(auto const& source:skinned_sources_) {
        vertex_count+=source.vertices.size();
        // Pad each joint table so each dispatch has an isolated palette.
        palette_count+=NORTHSTARCPP_SKIN_MAX_JOINTS*16u;
    }
    if(vertex_count*sizeof(NorthStarSkinMaterialVertex)>max_skin_bytes ||
       palette_count*sizeof(float)>max_bone_bytes)
        throw std::runtime_error("GPU skin staging buffers exceeded");
    void* vertices=nullptr;
    void* bones=nullptr;
    check(vkMapMemory(device_,skin_input_memory_,0,
        vertex_count*sizeof(NorthStarSkinMaterialVertex),0,&vertices),
        "vkMapMemory(GPU skin vertices)");
    try {
        check(vkMapMemory(device_,skin_palette_memory_,0,
            palette_count*sizeof(float),0,&bones),
            "vkMapMemory(GPU bone matrices)");
        std::uint64_t vertex_offset=0;
        for(auto const& source:skinned_sources_) {
            const auto bytes=source.vertices.size()*sizeof(NorthStarSkinMaterialVertex);
            std::memcpy(static_cast<std::uint8_t*>(vertices)+
                vertex_offset*sizeof(NorthStarSkinMaterialVertex),
                source.vertices.data(),bytes);
            vertex_offset+=source.vertices.size();
        }
        auto* dest=static_cast<float*>(bones);
        for(auto const& source:skinned_sources_) {
            std::fill(dest,dest+NORTHSTARCPP_SKIN_MAX_JOINTS*16u,0.f);
            std::memcpy(dest,source.palette.data(),source.palette.size()*sizeof(float));
            dest+=NORTHSTARCPP_SKIN_MAX_JOINTS*16u;
        }
        vkUnmapMemory(device_,skin_palette_memory_);
    }catch(...) {
        vkUnmapMemory(device_,skin_input_memory_);
        throw;
    }
    vkUnmapMemory(device_,skin_input_memory_);
    std::uint32_t first=0;
    for(std::size_t index=0;index<skinned_sources_.size();++index) {
        SkinDraw draw{};
        draw.first=first;
        draw.count=static_cast<std::uint32_t>(skinned_sources_[index].vertices.size());
        draw.textures=material_sets_.at(material_sources_.size()+index);
        draw.pipeline=material_graph_pipeline(
            skinned_sources_[index].material.variant_prefix,0);
        skin_draws_.push_back(draw);
        first+=draw.count;
    }
}
void Renderer::record_skin_compute(VkCommandBuffer command,
                                    const NorthStarRenderFrame& frame) {
    if(skin_draws_.empty())return;
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,skin_pipeline_);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,
        skin_pipeline_layout_,0,1,&skin_set_,0,nullptr);
    float const near_clip=frame.camera_near_clip>0.f?
        frame.camera_near_clip:.1f;
    float const far_clip=frame.camera_far_clip>near_clip?
        frame.camera_far_clip:250.f;
    float const fov=frame.camera_fov_y_degrees>1.f?
        frame.camera_fov_y_degrees:70.f;
    const float factor=1.f/std::tan(fov*3.14159265f/360.f);
    const float aspect=static_cast<float>(extent_.width)/extent_.height;
    const float cy=std::cos(frame.camera_yaw),sy=std::sin(frame.camera_yaw);
    const float cp=std::cos(frame.camera_pitch),sp=std::sin(frame.camera_pitch);
    for(std::size_t i=0;i<skin_draws_.size();++i) {
        const auto& draw=skin_draws_[i];
        const auto& material=skinned_sources_[i].material;
        SkinComputePush push{};
        push.camera[0]=frame.camera_x;push.camera[1]=frame.camera_y;
        push.camera[2]=frame.camera_z;
        push.angles[0]=cy;push.angles[1]=sy;
        push.angles[2]=cp;push.angles[3]=sp;
        push.projection[0]=factor;push.projection[1]=aspect;
        push.projection[2]=near_clip;push.projection[3]=far_clip;
        std::memcpy(push.tint,material.tint.data(),sizeof(push.tint));
        unsigned textures=0;
        for(unsigned slot=0;slot<5;++slot)
            if(!material.textures[slot].empty())textures|=1u<<slot;
        push.params[0]=material.metallic;
        push.params[1]=material.roughness;
        push.params[2]=material.unlit?1.f:0.f;
        push.params[3]=static_cast<float>(textures);
        push.offsets[0]=draw.first;push.offsets[1]=draw.first;
        push.offsets[2]=draw.count;
        push.offsets[3]=static_cast<std::uint32_t>(
            i*NORTHSTARCPP_SKIN_MAX_JOINTS*16u);
        vkCmdPushConstants(command,skin_pipeline_layout_,
            VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);
        vkCmdDispatch(command,(draw.count+63)/64,1,1);
    }
    VkBufferMemoryBarrier2 transition{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    transition.srcStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    transition.srcAccessMask=VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    transition.dstStageMask=VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
    transition.dstAccessMask=VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    transition.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    transition.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    transition.buffer=skin_output_;
    transition.offset=0;
    transition.size=max_skin_bytes;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount=1;
    dependency.pBufferMemoryBarriers=&transition;
    vkCmdPipelineBarrier2(command,&dependency);
}
} // namespace northstarcpp::render::vulkan
