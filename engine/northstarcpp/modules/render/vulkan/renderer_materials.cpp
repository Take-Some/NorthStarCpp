#include "../shared/engine_sky_generated.hpp"
// Vulkan material ABI v1. Native GPU texture sampling and GGX PBR.
// CPU copies of all host-owned vertices/texels, bounded and lazy GPU uploads.
#include "renderer_private.hpp"
#include "renderer_material_submission.hpp"
#include "../../shared/frame_memory_counters.hpp"
#include "material_spirv.hpp"
#include "material_graph_glsl.hpp"
#include "../shared/render_light_validation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace frame_memory = northstarcpp::diagnostics::frame_memory;

namespace northstarcpp::render::vulkan {
namespace {
void check(VkResult result,const char* name) {
    if(result!=VK_SUCCESS)
        throw std::runtime_error(std::string(name)+" failed: VkResult="+
                                 std::to_string(static_cast<int>(result)));
}
std::uint64_t hash_texture(std::uint8_t const* data,std::size_t count,
                           std::uint32_t width,std::uint32_t height,VkFormat format) {
    std::uint64_t hash=14695981039346656037ull;
    auto add=[&](std::uint8_t n) {hash^=n;hash*=1099511628211ull;};
    for(unsigned i=0;i<4;++i) {
        add(static_cast<std::uint8_t>(width>>(i*8)));
        add(static_cast<std::uint8_t>(height>>(i*8)));
        add(static_cast<std::uint8_t>(static_cast<unsigned>(format)>>(i*8)));
    }
    for(std::size_t i=0;i<count;++i)add(data[i]);
    return hash;
}
std::array<std::uint8_t,4> fallback_rgba(std::uint32_t slot) {
    if(slot==1)return {128,128,255,255}; // +Z tangent-space normal
    if(slot==4)return {0,0,0,255};      // no emission
    return {255,255,255,255};            // identity for albedo, roughness, metal
}
}
void Renderer::set_material_batches(const NorthStarMaterialBatch* batches,
                                    std::uint32_t count) {
    if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES || (count&&!batches))
        throw std::invalid_argument("Vulkan material batch count/pointer invalid");
    std::vector<MaterialSource> next;
    frame_memory::reserve(next,count);
    std::uint64_t total_vertices=0;
    // Scene sky is an additional stable unlit material batch, not an
    // authored material index and must not invalidate ordinary batches.
    bool textures_same=(count+(sky_enabled_?1u:0u)==material_sources_.size()) &&
                       !sky_pending_;
    VkPhysicalDeviceProperties gpu_properties{};
    vkGetPhysicalDeviceProperties(physical_,&gpu_properties);
    for(std::uint32_t i=0;i<count;++i) {
        auto const& input=batches[i];
        if(!input.vertices||input.vertex_count==0||
           input.vertex_count%3||input.vertex_count>NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES)
            throw std::invalid_argument("Vulkan material triangles are invalid");
        total_vertices+=input.vertex_count;
        if(total_vertices*sizeof(MaterialClipVertex)>max_material_vertex_bytes_)
            throw std::invalid_argument("Vulkan material vertex capacity exceeded");
        if(!std::isfinite(input.metallic)||!std::isfinite(input.roughness)||
           input.metallic<0||input.metallic>1||
           input.roughness<0||input.roughness>1||input.unlit>1)
            throw std::invalid_argument("Invalid Vulkan PBR material parameters");
        MaterialSource source;
        auto const* previous=i<material_sources_.size() &&
            i!=material_sky_index_?&material_sources_[i]:nullptr;
        for(unsigned component=0;component<4;++component) {
            if(!std::isfinite(input.base_color[component]))
                throw std::invalid_argument("Invalid material base color");
            source.tint[component]=input.base_color[component];
        }
        source.roughness=input.roughness;
        source.metallic=input.metallic;
        source.unlit=input.unlit;
        for(std::uint32_t k=0;k<NORTHSTARCPP_RENDER_MATERIAL_TEXTURE_SLOTS;++k) {
            auto const& image=input.textures[k];
            if(!image.rgba8) {
                if(image.width||image.height||image.data_bytes)
                    throw std::invalid_argument("Incomplete Vulkan texture slot");
                textures_same &= stage_material_texture(source,previous,k,image);
                continue;
            }
            if(!image.width||!image.height||
               image.width>gpu_properties.limits.maxImageDimension2D||
               image.height>gpu_properties.limits.maxImageDimension2D||
               static_cast<std::uint64_t>(image.width)*image.height*4!=image.data_bytes)
                throw std::invalid_argument("Malformed Vulkan RGBA8 material texture");
            textures_same &= stage_material_texture(source,previous,k,image);
        }
        if(!previous || !matches_geometry(previous->vertices,input.vertices,input.vertex_count)) {
            for(std::uint32_t k=0;k<input.vertex_count;++k) {
                auto const& vertex=input.vertices[k];
                if(!std::isfinite(vertex.x)||!std::isfinite(vertex.y)||
                   !std::isfinite(vertex.z)||!std::isfinite(vertex.nx)||
                   !std::isfinite(vertex.ny)||!std::isfinite(vertex.nz)||
                   !std::isfinite(vertex.u)||!std::isfinite(vertex.v))
                    throw std::invalid_argument("Vulkan material geometry contains NaN/Inf");
            }
            frame_memory::assign(source.vertices,input.vertices,input.vertices+input.vertex_count,
                &NorthStarFrameMemoryCounters::geometry_copy_bytes);
        }
        next.push_back(std::move(source));
    }
    for(std::size_t i=0;i<std::min(next.size(),material_sources_.size());++i) {
        if(i==material_sky_index_)continue;
        adopt_retained_textures(next[i],material_sources_[i]);
        if(next[i].vertices.empty())next[i].vertices.swap(material_sources_[i].vertices);
    }
    if(sky_enabled_) {
        if(sky_pending_) {
            next.push_back(std::move(sky_pending_source_));
            sky_pending_=false;
            textures_same=false;
        } else if(material_sky_index_<material_sources_.size()) {
            next.push_back(std::move(material_sources_[material_sky_index_]));
        } else {
            throw std::logic_error("Vulkan sky material residency lost");
        }
        material_sky_index_=count;
    } else {
        material_sky_index_=static_cast<std::size_t>(-1);
    }
    material_sources_=std::move(next); // Atomic update after all validation.
    if(!textures_same)material_textures_dirty_=true;
}
void Renderer::set_material_batches_v2(
    const NorthStarMaterialBatchV2* batches,std::uint32_t count)
{
    if(count>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES || (count&&!batches))
        throw std::invalid_argument("Invalid Vulkan Material Graph v2 batches");
    std::vector<NorthStarMaterialBatch> base;
    std::vector<std::string> programs;
    frame_memory::reserve(base,count);programs.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        const auto& batch=batches[i];
        if(!std::isfinite(batch.opacity)||batch.opacity<0.f||batch.opacity>1.f)
            throw std::invalid_argument("Invalid Vulkan material opacity");
        auto program=validated_material_graph_glsl(
            batch.graph_hlsl_defines,batch.graph_hlsl_bytes,
            batch.blend_mode,batch.alpha_cutoff);
        programs.push_back(
            (batch.graph_hlsl_bytes||batch.blend_mode!=0)?program:std::string{});
        auto material=batch.material;
        material.base_color[3]*=batch.opacity;
        base.push_back(material);
    }
    // The v1 update validates/copied geometry and textures atomically.
    set_material_batches(base.data(),count);
    for(std::uint32_t i=0;i<count;++i) {
        material_sources_[i].variant_prefix=std::move(programs[i]);
        material_sources_[i].blend_mode=batches[i].blend_mode;
        material_sources_[i].alpha_cutoff=batches[i].alpha_cutoff;
    }
}
void Renderer::set_lights(const NorthStarRenderLight* lights,
                          std::uint32_t count) {
    dynamic_lights_=northstarcpp::render::validated_render_lights(lights,count);
    lights_dirty_=true;
}
void Renderer::upload_light_buffer() {
    if(!lights_dirty_)return;
    constexpr auto size=16+sizeof(NorthStarRenderLight)*
        NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS;
    std::array<std::uint8_t,size> bytes{};
    auto const count=static_cast<float>(dynamic_lights_.size());
    std::memcpy(bytes.data(),&count,sizeof(count));
    if(!dynamic_lights_.empty())
        std::memcpy(bytes.data()+16,dynamic_lights_.data(),
                    dynamic_lights_.size()*sizeof(NorthStarRenderLight));
    void* mapped=nullptr;
    check(vkMapMemory(device_,lights_memory_,0,size,0,&mapped),
          "vkMapMemory(dynamic lights)");
    std::memcpy(mapped,bytes.data(),bytes.size());
    vkUnmapMemory(device_,lights_memory_);
    lights_dirty_=false;
}
// Full-resolution vertex streams grow on demand; native YDR is never
// tessellated down or replaced by proxy meshes. A small empty scene keeps a
// small upload buffer. Growth happens only after the previous GPU work ends.
void Renderer::ensure_material_vertex_capacity(VkDeviceSize required_bytes) {
    if(required_bytes<=material_vertex_capacity_bytes_)return;
    if(required_bytes>max_material_vertex_bytes_)
        throw std::length_error("Vulkan high-detail material capacity exceeded");
    VkDeviceSize next=std::max<VkDeviceSize>(material_vertex_capacity_bytes_,
                                              4ull*1024*1024);
    while(next<required_bytes)
        next=std::min(next*2,max_material_vertex_bytes_);
    VkBufferCreateInfo descriptor{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    descriptor.size=next;
    descriptor.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VkBuffer next_buffer=VK_NULL_HANDLE;
    check(vkCreateBuffer(device_,&descriptor,nullptr,&next_buffer),
          "vkCreateBuffer(high-detail material vertices)");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_,next_buffer,&requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize=requirements.size;
    allocation.memoryTypeIndex=find_memory_type(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory next_memory=VK_NULL_HANDLE;
    auto result=vkAllocateMemory(device_,&allocation,nullptr,&next_memory);
    if(result!=VK_SUCCESS) {
        vkDestroyBuffer(device_,next_buffer,nullptr);
        check(result,"vkAllocateMemory(high-detail material vertices)");
    }
    result=vkBindBufferMemory(device_,next_buffer,next_memory,0);
    if(result!=VK_SUCCESS) {
        vkFreeMemory(device_,next_memory,nullptr);
        vkDestroyBuffer(device_,next_buffer,nullptr);
        check(result,"vkBindBufferMemory(high-detail material vertices)");
    }
    check(diagnostics_.time_wait(VulkanWaitKind::device_idle, [&] { return vkDeviceWaitIdle(device_); }),"vkDeviceWaitIdle(material buffer resize)");
    if(material_vertices_buffer_)
        vkDestroyBuffer(device_,material_vertices_buffer_,nullptr);
    if(material_vertices_memory_)
        vkFreeMemory(device_,material_vertices_memory_,nullptr);
    material_vertices_buffer_=next_buffer;
    material_vertices_memory_=next_memory;
    material_vertex_capacity_bytes_=next;
    diagnostics_.add(VulkanCounter::material_buffer_grows);
    std::fprintf(stderr,
        "[INFO] [engine.render] vulkan.material_vertex_capacity_bytes=%llu\\n",
        static_cast<unsigned long long>(next));
}

void Renderer::create_material_resources() {
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    material_vertex_capacity_bytes_=4ull*1024*1024;
    buffer.size=material_vertex_capacity_bytes_;
    buffer.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    check(vkCreateBuffer(device_,&buffer,nullptr,&material_vertices_buffer_),
          "vkCreateBuffer(material vertices)");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_,material_vertices_buffer_,&req);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize=req.size;
    allocation.memoryTypeIndex=find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_,&allocation,nullptr,&material_vertices_memory_),
          "vkAllocateMemory(material vertices)");
    check(vkBindBufferMemory(device_,material_vertices_buffer_,material_vertices_memory_,0),
          "vkBindBufferMemory(material vertices)");

    constexpr auto lightingBytes=16+sizeof(NorthStarRenderLight)*
        NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS;
    VkBufferCreateInfo lighting{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    lighting.size=lightingBytes;lighting.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    check(vkCreateBuffer(device_,&lighting,nullptr,&lights_buffer_),
          "vkCreateBuffer(dynamic lights)");
    vkGetBufferMemoryRequirements(device_,lights_buffer_,&req);
    VkMemoryAllocateInfo lightAllocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    lightAllocation.allocationSize=req.size;
    lightAllocation.memoryTypeIndex=find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_,&lightAllocation,nullptr,&lights_memory_),
          "vkAllocateMemory(dynamic lights)");
    check(vkBindBufferMemory(device_,lights_buffer_,lights_memory_,0),
          "vkBindBufferMemory(dynamic lights)");

    VkBufferCreateInfo skyBuffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    skyBuffer.size=sizeof(NorthStarGpuSkyUniforms);
    skyBuffer.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    check(vkCreateBuffer(device_,&skyBuffer,nullptr,&sky_uniform_buffer_),
          "vkCreateBuffer(GPU Sky uniforms)");
    vkGetBufferMemoryRequirements(device_,sky_uniform_buffer_,&req);
    VkMemoryAllocateInfo skyAllocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    skyAllocation.allocationSize=req.size;
    skyAllocation.memoryTypeIndex=find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_,&skyAllocation,nullptr,&sky_uniform_memory_),
          "vkAllocateMemory(GPU Sky uniforms)");
    check(vkBindBufferMemory(device_,sky_uniform_buffer_,sky_uniform_memory_,0),
          "vkBindBufferMemory(GPU Sky uniforms)");

    std::array<VkDescriptorSetLayoutBinding,7> bindings{};
    for(std::uint32_t i=0;i<5;++i) {
        bindings[i].binding=i;bindings[i].descriptorType=
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount=1;
        bindings[i].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[5].binding=5;
    bindings[5].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[5].descriptorCount=1;
    bindings[5].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[6].binding=6;
    bindings[6].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[6].descriptorCount=1;
    bindings[6].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount=static_cast<std::uint32_t>(bindings.size());
    layout.pBindings=bindings.data();
    check(vkCreateDescriptorSetLayout(device_,&layout,nullptr,&material_set_layout_),
          "vkCreateDescriptorSetLayout(material)");
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter=VK_FILTER_LINEAR;
    sampler.minFilter=VK_FILTER_LINEAR;
    sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod=0;
    check(vkCreateSampler(device_,&sampler,nullptr,&material_sampler_),
          "vkCreateSampler(material)");
    material_textures_dirty_=true;
}
void Renderer::reset_material_textures() {
    for(auto& item:material_texture_cache_) {
        auto& texture=item.second;
        if(texture.view)vkDestroyImageView(device_,texture.view,nullptr);
        if(texture.image)vkDestroyImage(device_,texture.image,nullptr);
        if(texture.memory)vkFreeMemory(device_,texture.memory,nullptr);
    }
    material_texture_cache_.clear();
    material_sets_.clear();
    if(material_descriptor_pool_)
        vkDestroyDescriptorPool(device_,material_descriptor_pool_,nullptr);
    material_descriptor_pool_=VK_NULL_HANDLE;
    const auto source_count=material_sources_.size()+skinned_sources_.size();
    if(source_count>UINT32_MAX/5u)
        throw std::length_error("Vulkan material descriptor count overflow");
    const auto capacity=static_cast<std::uint32_t>(std::max<std::size_t>(1,source_count));
    VkDescriptorPoolSize sizes[2]{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,5u*capacity},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2u*capacity}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=capacity;
    pool.poolSizeCount=2;pool.pPoolSizes=sizes;
    check(vkCreateDescriptorPool(device_,&pool,nullptr,&material_descriptor_pool_),
          "vkCreateDescriptorPool(material)");
}
void Renderer::destroy_material_resources() noexcept {
    if(!device_)return;
    for(auto& item:material_texture_cache_) {
        auto& tex=item.second;
        if(tex.view)vkDestroyImageView(device_,tex.view,nullptr);
        if(tex.image)vkDestroyImage(device_,tex.image,nullptr);
        if(tex.memory)vkFreeMemory(device_,tex.memory,nullptr);
    }
    material_texture_cache_.clear();
    if(material_descriptor_pool_)vkDestroyDescriptorPool(device_,material_descriptor_pool_,nullptr);
    if(material_sampler_)vkDestroySampler(device_,material_sampler_,nullptr);
    if(material_set_layout_)vkDestroyDescriptorSetLayout(device_,material_set_layout_,nullptr);
    if(material_vertices_buffer_)vkDestroyBuffer(device_,material_vertices_buffer_,nullptr);
    if(material_vertices_memory_)vkFreeMemory(device_,material_vertices_memory_,nullptr);
    if(lights_buffer_)vkDestroyBuffer(device_,lights_buffer_,nullptr);
    if(lights_memory_)vkFreeMemory(device_,lights_memory_,nullptr);
    if(sky_uniform_buffer_)vkDestroyBuffer(device_,sky_uniform_buffer_,nullptr);
    if(sky_uniform_memory_)vkFreeMemory(device_,sky_uniform_memory_,nullptr);
    sky_uniform_buffer_=VK_NULL_HANDLE;
    sky_uniform_memory_=VK_NULL_HANDLE;
    material_descriptor_pool_=VK_NULL_HANDLE;
    material_sampler_=VK_NULL_HANDLE;
    material_set_layout_=VK_NULL_HANDLE;
    material_vertices_buffer_=VK_NULL_HANDLE;
    material_vertices_memory_=VK_NULL_HANDLE;
    lights_buffer_=VK_NULL_HANDLE;lights_memory_=VK_NULL_HANDLE;
    dynamic_lights_.clear();lights_dirty_=true;
    material_sets_.clear();material_sources_.clear();material_draws_.clear();
    material_frame_vertices_.clear();
}
MaterialTextureGpu Renderer::upload_material_texture(
    const MaterialSource& source,std::uint32_t slot)
{
    auto const& values=source.textures[slot];
    auto const defaults=fallback_rgba(slot);
    auto const* data=values.empty()?defaults.data():values.data();
    auto const count=values.empty()?defaults.size():values.size();
    auto const width=values.empty()?1u:source.widths[slot];
    auto const height=values.empty()?1u:source.heights[slot];
    auto const format=(slot==4||(slot==0&&!source.sky_cloud_fields))?
        VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;

    MaterialTextureGpu texture;
    texture.width=width;texture.height=height;texture.format=format;
    frame_memory::assign(texture.source_bytes,data,data+count,
        &NorthStarFrameMemoryCounters::texture_copy_bytes);
    auto const& raw=texture.source_bytes;
    diagnostics_.add(VulkanCounter::texture_upload_bytes,raw.size());
    diagnostics_.add(VulkanCounter::texture_uploads);
    VkBuffer staging=VK_NULL_HANDLE;
    VkDeviceMemory staging_memory=VK_NULL_HANDLE;
    VkCommandBuffer cmd=VK_NULL_HANDLE;
    auto cleanup_temp=[&] {
        if(cmd)vkFreeCommandBuffers(device_,command_pool_,1,&cmd);
        if(staging)vkDestroyBuffer(device_,staging,nullptr);
        if(staging_memory)vkFreeMemory(device_,staging_memory,nullptr);
        cmd=VK_NULL_HANDLE;staging=VK_NULL_HANDLE;
        staging_memory=VK_NULL_HANDLE;
    };
    try {
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType=VK_IMAGE_TYPE_2D;image.extent={width,height,1};
        image.format=format;image.mipLevels=1;image.arrayLayers=1;
        image.samples=VK_SAMPLE_COUNT_1_BIT;
        image.tiling=VK_IMAGE_TILING_OPTIMAL;
        image.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        image.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device_,&image,nullptr,&texture.image),
              "vkCreateImage(material)");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_,texture.image,&req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize=req.size;
        ai.memoryTypeIndex=find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_,&ai,nullptr,&texture.memory),
              "vkAllocateMemory(material)");
        check(vkBindImageMemory(device_,texture.image,texture.memory,0),
              "vkBindImageMemory(material)");
        VkImageViewCreateInfo iv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        iv.image=texture.image;iv.viewType=VK_IMAGE_VIEW_TYPE_2D;
        iv.format=format;iv.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.levelCount=1;iv.subresourceRange.layerCount=1;
        check(vkCreateImageView(device_,&iv,nullptr,&texture.view),
              "vkCreateImageView(material)");

        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size=raw.size();buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        check(vkCreateBuffer(device_,&buffer,nullptr,&staging),
              "vkCreateBuffer(material staging)");
        vkGetBufferMemoryRequirements(device_,staging,&req);
        ai.allocationSize=req.size;
        ai.memoryTypeIndex=find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device_,&ai,nullptr,&staging_memory),
              "vkAllocateMemory(material staging)");
        check(vkBindBufferMemory(device_,staging,staging_memory,0),
              "vkBindBufferMemory(material staging)");
        void* mapped=nullptr;
        check(vkMapMemory(device_,staging_memory,0,raw.size(),0,&mapped),
              "vkMapMemory(material staging)");
        std::memcpy(mapped,raw.data(),raw.size());
        frame_memory::copied(&NorthStarFrameMemoryCounters::texture_copy_bytes,raw.size());
        vkUnmapMemory(device_,staging_memory);

        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool=command_pool_;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount=1;
        check(vkAllocateCommandBuffers(device_,&alloc,&cmd),
              "vkAllocateCommandBuffers(material upload)");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd,&begin),"vkBeginCommandBuffer(material upload)");
        VkImageSubresourceRange range{};
        range.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount=1;range.layerCount=1;
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask=VK_PIPELINE_STAGE_2_NONE;
        barrier.srcAccessMask=VK_ACCESS_2_NONE;
        barrier.dstStageMask=VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.dstAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=texture.image;barrier.subresourceRange=range;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount=1;
        dependency.pImageMemoryBarriers=&barrier;
        vkCmdPipelineBarrier2(cmd,&dependency);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount=1;
        copy.imageExtent={width,height,1};
        vkCmdCopyBufferToImage(cmd,staging,texture.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.srcStageMask=VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask=VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask=VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier2(cmd,&dependency);
        check(vkEndCommandBuffer(cmd),"vkEndCommandBuffer(material upload)");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        check(vkQueueSubmit(queue_,1,&submit,VK_NULL_HANDLE),
              "vkQueueSubmit(material upload)");
        check(diagnostics_.time_wait(VulkanWaitKind::texture_queue_idle, [&] { return vkQueueWaitIdle(queue_); }),"vkQueueWaitIdle(material upload)");
        cleanup_temp();
        return texture;
    }catch(...) {
        // Submission failures still demand a best-effort idle before freeing
        // resources potentially referenced by a queued copy command.
        diagnostics_.time_wait(VulkanWaitKind::texture_queue_idle, [&] { return vkQueueWaitIdle(queue_); });
        cleanup_temp();
        if(texture.view)vkDestroyImageView(device_,texture.view,nullptr);
        if(texture.image)vkDestroyImage(device_,texture.image,nullptr);
        if(texture.memory)vkFreeMemory(device_,texture.memory,nullptr);
        throw;
    }
}
void Renderer::upload_material_textures() {
    if(!material_textures_dirty_)return;
    diagnostics_.add(VulkanCounter::texture_descriptor_rebuilds);
    check(diagnostics_.time_wait(VulkanWaitKind::device_idle, [&] { return vkDeviceWaitIdle(device_); }),"vkDeviceWaitIdle(material refresh)");
    reset_material_textures();
    const auto total=material_sources_.size()+skinned_sources_.size();
    material_sets_.resize(total,VK_NULL_HANDLE);
    for(std::size_t batch_id=0;batch_id<total;++batch_id) {
        auto const& source=batch_id<material_sources_.size()?
            material_sources_[batch_id]:
            skinned_sources_[batch_id-material_sources_.size()].material;
        std::array<VkDescriptorImageInfo,5> images{};
        for(std::uint32_t slot=0;slot<5;++slot) {
            auto const& bytes=source.textures[slot];
            auto const defaults=fallback_rgba(slot);
            auto const* data=bytes.empty()?defaults.data():bytes.data();
            auto const count=bytes.empty()?defaults.size():bytes.size();
            auto const width=bytes.empty()?1u:source.widths[slot];
            auto const height=bytes.empty()?1u:source.heights[slot];
            auto const format=(slot==4||(slot==0&&!source.sky_cloud_fields))?
                VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;
            auto key=hash_texture(data,count,width,height,format);
            while(true) {
                auto it=material_texture_cache_.find(key);
                if(it==material_texture_cache_.end()) {
                    auto image=upload_material_texture(source,slot);
                    it=material_texture_cache_.emplace(key,std::move(image)).first;
                }
                if(it->second.width==width&&it->second.height==height &&
                   it->second.format==format&&it->second.source_bytes.size()==count&&
                   std::memcmp(it->second.source_bytes.data(),data,count)==0) {
                    images[slot]={material_sampler_,it->second.view,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                    break;
                }
                ++key; // Deterministic handling of an actual hash collision.
            }
        }
        VkDescriptorSetAllocateInfo allocate{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool=material_descriptor_pool_;
        allocate.descriptorSetCount=1;
        allocate.pSetLayouts=&material_set_layout_;
        check(vkAllocateDescriptorSets(device_,&allocate,&material_sets_[batch_id]),
              "vkAllocateDescriptorSets(material)");
        std::array<VkWriteDescriptorSet,5> writes{};
        for(std::uint32_t slot=0;slot<5;++slot) {
            auto& write=writes[slot];
            write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet=material_sets_[batch_id];
            write.dstBinding=slot;
            write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount=1;
            write.pImageInfo=&images[slot];
        }
        VkDescriptorBufferInfo lightBuffer{lights_buffer_,0,
            16+sizeof(NorthStarRenderLight)*NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS};
        VkWriteDescriptorSet lightWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        lightWrite.dstSet=material_sets_[batch_id];
        lightWrite.dstBinding=5;
        lightWrite.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lightWrite.descriptorCount=1;
        lightWrite.pBufferInfo=&lightBuffer;
        vkUpdateDescriptorSets(device_,static_cast<std::uint32_t>(writes.size()),
                               writes.data(),0,nullptr);
        vkUpdateDescriptorSets(device_,1,&lightWrite,0,nullptr);
        VkDescriptorBufferInfo skyBufferInfo{
            sky_uniform_buffer_,0,sizeof(NorthStarGpuSkyUniforms)};
        VkWriteDescriptorSet skyWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        skyWrite.dstSet=material_sets_[batch_id];
        skyWrite.dstBinding=6;
        skyWrite.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        skyWrite.descriptorCount=1;
        skyWrite.pBufferInfo=&skyBufferInfo;
        vkUpdateDescriptorSets(device_,1,&skyWrite,0,nullptr);
    }
    material_textures_dirty_=false;
}
void Renderer::create_material_pipeline() {
    const auto shader=[&](const std::uint32_t* words,std::size_t count,
                          VkShaderModule& target) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize=count*4;ci.pCode=words;
        check(vkCreateShaderModule(device_,&ci,nullptr,&target),
              "vkCreateShaderModule(material)");
    };
    shader(shaders::material_vertex,std::size(shaders::material_vertex),
           material_vertex_shader_);
    shader(shaders::material_fragment,std::size(shaders::material_fragment),
           material_fragment_shader_);
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount=1;layout.pSetLayouts=&material_set_layout_;
    check(vkCreatePipelineLayout(device_,&layout,nullptr,&material_pipeline_layout_),
          "vkCreatePipelineLayout(material)");
    material_pipeline_=create_pipeline_for_material_fragment(
        material_fragment_shader_,0);
    if(deferred_enabled_) {
        VkShaderModuleCreateInfo gb{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        gb.codeSize=sizeof(shaders::material_gbuffer_fragment);
        gb.pCode=shaders::material_gbuffer_fragment;
        VkShaderModule module=VK_NULL_HANDLE;
        check(vkCreateShaderModule(device_,&gb,nullptr,&module),
              "vkCreateShaderModule(material GBuffer)");
        try {
            material_gbuffer_pipeline_=create_pipeline_for_material_fragment(
                module,0,true);
        } catch(...) {
            vkDestroyShaderModule(device_,module,nullptr);
            throw;
        }
        vkDestroyShaderModule(device_,module,nullptr);
    }
}
VkPipeline Renderer::create_pipeline_for_material_fragment(
    VkShaderModule fragment,std::uint32_t blend_mode,bool deferred,
    VkShaderModule vertex_override) {
    if(deferred && blend_mode==2)
        throw std::invalid_argument("Translucent GBuffer material is unsupported");
    if(!fragment||blend_mode>2)
        throw std::invalid_argument("Invalid Vulkan material pipeline variant");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(unsigned i=0;i<2;++i) {
        stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module=i?fragment:(vertex_override?vertex_override:material_vertex_shader_);
        stages[i].pName="main";
    }
    VkVertexInputBindingDescription binding{0,sizeof(MaterialClipVertex),
                                             VK_VERTEX_INPUT_RATE_VERTEX};
    std::array<VkVertexInputAttributeDescription,6> attributes{{
        {0,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(MaterialClipVertex,clip)},
        {1,0,VK_FORMAT_R32G32_SFLOAT,offsetof(MaterialClipVertex,uv)},
        {2,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(MaterialClipVertex,normal)},
        {3,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(MaterialClipVertex,tint)},
        {4,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(MaterialClipVertex,params)},
        {5,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(MaterialClipVertex,world)}
    }};
    VkPipelineVertexInputStateCreateInfo vertex{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;
    vertex.vertexAttributeDescriptionCount=static_cast<std::uint32_t>(attributes.size());
    vertex.pVertexAttributeDescriptions=attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount=1;viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL;
    raster.cullMode=VK_CULL_MODE_NONE;
    raster.frontFace=VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth=1.f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable=VK_TRUE;
    depth.depthWriteEnable=blend_mode==2?VK_FALSE:VK_TRUE;
    depth.depthCompareOp=VK_COMPARE_OP_LESS;
    std::array<VkPipelineColorBlendAttachmentState,4> writes{};
    for(auto& write:writes)
        write.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                             VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    if(blend_mode==2) {
        writes[0].blendEnable=VK_TRUE;
        writes[0].srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
        writes[0].dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        writes[0].colorBlendOp=VK_BLEND_OP_ADD;
        writes[0].srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
        writes[0].dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        writes[0].alphaBlendOp=VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount=deferred?(gbuffer_v2_?4u:3u):1u;
    blend.pAttachments=writes.data();
    VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamic_states;
    VkPipelineRenderingCreateInfo rendering{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount=deferred?(gbuffer_v2_?4u:3u):1u;
    VkFormat forward_format=hdr_enabled_?hdr_format_:format_;
    rendering.pColorAttachmentFormats=deferred?gbuffer_formats_.data():&forward_format;
    rendering.depthAttachmentFormat=depth_format_;
    VkGraphicsPipelineCreateInfo pipeline{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.pNext=&rendering;
    pipeline.stageCount=2;pipeline.pStages=stages;
    pipeline.pVertexInputState=&vertex;
    pipeline.pInputAssemblyState=&assembly;
    pipeline.pViewportState=&viewport;
    pipeline.pRasterizationState=&raster;
    pipeline.pMultisampleState=&multisample;
    pipeline.pDepthStencilState=&depth;
    pipeline.pColorBlendState=&blend;
    pipeline.pDynamicState=&dynamic;
    pipeline.layout=material_pipeline_layout_;
    VkPipeline created=VK_NULL_HANDLE;
    check(create_cached_graphics_pipelines(1,&pipeline,&created),
          "vkCreateGraphicsPipelines(material)");
    return created;
}
VkPipeline Renderer::material_graph_pipeline(
    const std::string& prefix,std::uint32_t blend_mode,bool deferred) {
    if(prefix.empty() && blend_mode==0)
        return deferred?material_gbuffer_pipeline_:material_pipeline_;
    auto key=(deferred?"G|":"F|")+std::to_string(blend_mode)+"|"+prefix;
    if(auto found=material_variant_pipelines_.find(key);
       found!=material_variant_pipelines_.end())return found->second;
    if(material_variant_pipelines_.size()>=128)
        throw std::runtime_error("Vulkan material graph PSO budget exceeded");
    std::string glsl(shaders::material_fragment_glsl);
    auto const directive=glsl.find('\n');
    if(directive==std::string::npos || glsl.rfind("#version",0)!=0)
        throw std::logic_error("Invalid embedded Vulkan Material Graph GLSL");
    glsl.insert(directive+1,
        (deferred?std::string("#define NS_GBUFFER 1\n"):std::string{})+prefix);
    if(prefix.find("NS_GPU_SKY")!=std::string::npos) {
        const auto main_pos=glsl.find("void main() {");
        if(main_pos==std::string::npos)
            throw std::logic_error("GPU Sky missing GLSL fragment entrypoint");
        glsl.insert(main_pos,northstarcpp::shaders::sky_atmosphere_glsl);
    }
    auto bytecode=compile_material_fragment_spirv(glsl);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize=bytecode.size()*sizeof(std::uint32_t);
    ci.pCode=bytecode.data();
    VkShaderModule module=VK_NULL_HANDLE;
    check(vkCreateShaderModule(device_,&ci,nullptr,&module),
          "vkCreateShaderModule(Material Graph SPIR-V)");
    try {
        auto pipeline=create_pipeline_for_material_fragment(
            module,blend_mode,deferred);
        material_variant_pipelines_.emplace(key,pipeline);
        vkDestroyShaderModule(device_,module,nullptr);
        return pipeline;
    }catch(...) {
        vkDestroyShaderModule(device_,module,nullptr);
        throw;
    }
}
void Renderer::collect_material_vertices(const NorthStarRenderFrame& frame) {
    material_frame_vertices_.clear();
    material_draws_.clear();
    if(material_sources_.empty())return;
    auto const near_clip=frame.camera_near_clip>0.f?
        frame.camera_near_clip:.1f;
    auto const far_clip=frame.camera_far_clip>near_clip?
        frame.camera_far_clip:250.f;
    auto const fov=frame.camera_fov_y_degrees>1.f?
        frame.camera_fov_y_degrees:70.f;
    auto const factor=1.f/std::tan(fov*3.14159265f/360.f);
    auto const aspect=static_cast<float>(extent_.width)/extent_.height;
    auto const cy=std::cos(frame.camera_yaw),sy=std::sin(frame.camera_yaw);
    auto const cp=std::cos(frame.camera_pitch),sp=std::sin(frame.camera_pitch);
    for(std::size_t index=0;index<material_sources_.size();++index) {
        auto const& batch=material_sources_[index];
        const bool sky=(index==material_sky_index_);
        MaterialDraw draw{};
        draw.first=static_cast<std::uint32_t>(material_frame_vertices_.size());
        draw.textures=material_sets_.at(index);
        draw.blend_mode=batch.blend_mode;
        // Render-provider-owned PSO override is looked up by explicit material ID.
        if(index<material_references_.size()) {
            auto it=hot_materials_.find(material_references_[index]);
            if(it!=hot_materials_.end() && it->second.installed_forward &&
               it->second.blend_mode==batch.blend_mode) {
                draw.pipeline=it->second.installed_forward;
                if(deferred_enabled_ && batch.blend_mode!=2)
                    draw.gbuffer_pipeline=it->second.installed_gbuffer;
            }
        }
        if(!draw.pipeline)draw.pipeline=material_graph_pipeline(
            (sky && gpu_sky_uniforms_.flags[0]>.5f)?
                std::string("#define NS_GPU_SKY 1\n"):
                batch.variant_prefix,batch.blend_mode);
        if(deferred_enabled_ && batch.blend_mode!=2)
            if(!draw.gbuffer_pipeline)draw.gbuffer_pipeline=material_graph_pipeline(
                (sky && gpu_sky_uniforms_.flags[0]>.5f)?
                    std::string("#define NS_GPU_SKY 1\n"):
                    batch.variant_prefix,batch.blend_mode,true);
        // GPU sky is a fullscreen background surface, not a physical mesh.
        // Dropping triangles whose vertices cross the near plane caused holes
        // that appeared/disappeared as the camera rotated. Six screen-space
        // vertices provide complete coverage at far depth, independently of
        // authored skydome size, winding or proximity to the camera.
        if(sky && gpu_sky_uniforms_.flags[0]>.5f) {
            constexpr std::array<std::array<float,2>,6> corners{{
                {{-1.f,-1.f}},{{-1.f,1.f}},{{1.f,-1.f}},
                {{1.f,-1.f}},{{-1.f,1.f}},{{1.f,1.f}}
            }};
            for(auto const& xy:corners) {
                MaterialClipVertex out{};
                out.clip[0]=xy[0];out.clip[1]=xy[1];
                out.clip[2]=.9999f;out.clip[3]=1.f;
                // Screen x/y -> camera-space ray -> world ray. Vulkan's
                // viewport has positive height and clip Y points down here.
                const float right=xy[0]*aspect/factor;
                const float vertical=-xy[1]/factor;
                const float forward=cp-vertical*sp;
                const float up=vertical*cp+sp;
                out.world[0]=right*cy+forward*sy;
                out.world[1]=up;
                out.world[2]=-right*sy+forward*cy;
                out.uv[0]=xy[0]*.5f+.5f;
                out.uv[1]=xy[1]*.5f+.5f;
                // Sky-only interpolation channel: camera world origin.
                // The fragment shader uses it to keep 3D cloud patterns
                // stationary when the player traverses the scene.
                out.normal[0]=frame.camera_x;
                out.normal[1]=frame.camera_y;
                out.normal[2]=frame.camera_z;
                std::memcpy(out.tint,batch.tint.data(),sizeof(out.tint));
                out.params[2]=1.f;
                material_frame_vertices_.push_back(out);
            }
            draw.count=6;
            material_draws_.push_back(draw);
            continue;
        }
        for(std::size_t i=0;i+2<batch.vertices.size();i+=3) {
            MaterialClipVertex tri[3]{};
            bool visible=true;
            for(unsigned j=0;j<3;++j) {
                auto const& v=batch.vertices[i+j];
                auto const dx=v.x-(sky?0.f:frame.camera_x);
                auto const dy=v.y-(sky?0.f:frame.camera_y);
                auto const dz=v.z-(sky?0.f:frame.camera_z);
                auto const horizontal=dx*cy-dz*sy;
                auto const forward=dx*sy+dz*cy;
                auto const vertical=dy*cp-forward*sp;
                auto const distance=dy*sp+forward*cp;
                // Authored sky triangles that cross the near plane are
                // clipped below. Rejecting the complete triangle produced
                // holes in custom skydomes when rotating the editor camera.
                if(!sky)visible=visible&&distance>near_clip;
                auto& out=tri[j];
                out.clip[0]=horizontal*factor/aspect;
                out.clip[1]=-vertical*factor;
                // A constant far depth makes the sky appear behind every
                // opaque object without a separate renderer-specific mesh
                // format. Its unlit texture bypasses world lighting.
                out.clip[2]=sky?distance*.9999f:
                    (far_clip*distance-far_clip*near_clip)/(far_clip-near_clip);
                out.clip[3]=distance;
                out.uv[0]=v.u;out.uv[1]=v.v;
                out.normal[0]=v.nx;out.normal[1]=v.ny;out.normal[2]=v.nz;
                std::memcpy(out.tint,batch.tint.data(),sizeof(out.tint));
                out.params[0]=batch.metallic;out.params[1]=batch.roughness;
                out.params[2]=batch.unlit?1.f:0.f;
                unsigned slots=0;
                for(unsigned slot=0;slot<5;++slot)
                    if(!batch.textures[slot].empty())slots|=1u<<slot;
                out.params[3]=static_cast<float>(slots);
                out.world[0]=v.x;out.world[1]=v.y;out.world[2]=v.z;
            }
            if(sky) {
                // Source YDD UVs and geometry must remain intact. Clip the
                // view-space polygon against Vulkan's positive near plane,
                // interpolating its attributes before perspective division.
                // The far depth is constant, so sky never occludes geometry.
                auto interpolate=[](MaterialClipVertex const& a,
                                    MaterialClipVertex const& b,float t) {
                    MaterialClipVertex out{};
                    for(int k=0;k<4;++k) {
                        out.clip[k]=a.clip[k]+(b.clip[k]-a.clip[k])*t;
                        out.tint[k]=a.tint[k]+(b.tint[k]-a.tint[k])*t;
                        out.params[k]=a.params[k]+(b.params[k]-a.params[k])*t;
                    }
                    for(int k=0;k<2;++k)
                        out.uv[k]=a.uv[k]+(b.uv[k]-a.uv[k])*t;
                    for(int k=0;k<3;++k) {
                        out.normal[k]=a.normal[k]+
                            (b.normal[k]-a.normal[k])*t;
                        out.world[k]=a.world[k]+(b.world[k]-a.world[k])*t;
                    }
                    return out;
                };
                std::array<MaterialClipVertex,5> clipped{};
                unsigned count=0;
                constexpr unsigned vertices=3;
                for(unsigned j=0;j<vertices;++j) {
                    auto const& a=tri[(j+vertices-1)%vertices];
                    auto const& b=tri[j];
                    bool a_inside=a.clip[3]>near_clip*1.001f;
                    bool b_inside=b.clip[3]>near_clip*1.001f;
                    if(a_inside!=b_inside) {
                        float t=(near_clip*1.001f-a.clip[3])/
                            (b.clip[3]-a.clip[3]);
                        clipped[count++]=interpolate(a,b,std::clamp(t,0.f,1.f));
                    }
                    if(b_inside)clipped[count++]=b;
                }
                for(unsigned j=1;j+1<count;++j) {
                    material_frame_vertices_.push_back(clipped[0]);
                    material_frame_vertices_.push_back(clipped[j]);
                    material_frame_vertices_.push_back(clipped[j+1]);
                }
            } else if(visible)
                material_frame_vertices_.insert(material_frame_vertices_.end(),
                                                 tri,tri+3);
        }
        draw.count=static_cast<std::uint32_t>(material_frame_vertices_.size())-
                   draw.first;
        if(draw.count)material_draws_.push_back(draw);
    }
    // Alpha compositing is order-dependent. Depth-write is disabled for
    // translucent PSOs, so submit *triangles* globally back-to-front,
    // irrespective of material/batch submission order. Stable ties preserve
    // the original authoring order. CPU-transform already computed clip.w as
    // positive view-space distance; no extra camera transform is necessary.
    auto& translucent_triangles=translucent_triangles_scratch_;
    translucent_triangles.clear();
    for(std::size_t draw_id=0;draw_id<material_draws_.size();++draw_id) {
        auto const& draw=material_draws_[draw_id];
        if(draw.blend_mode!=2u)continue;
        for(std::uint32_t first=draw.first;first+2<draw.first+draw.count;
            first+=3u) {
            auto const& a=material_frame_vertices_[first];
            auto const& b=material_frame_vertices_[first+1u];
            auto const& c=material_frame_vertices_[first+2u];
            float depth=(a.clip[3]+b.clip[3]+c.clip[3])/3.f;
            translucent_triangles.push_back({depth,draw_id,first});
        }
    }
    if(!translucent_triangles.empty()) {
        std::stable_sort(translucent_triangles.begin(),
            translucent_triangles.end(),[](auto const& a,auto const& b) {
                return a.distance>b.distance;
            });
        auto& ordered=translucent_vertices_scratch_;
        ordered.clear();
        if(ordered.capacity()<material_frame_vertices_.size())
            ordered.reserve(material_frame_vertices_.size());
        auto& ordered_draws=translucent_draws_scratch_;
        ordered_draws.clear();
        if(ordered_draws.capacity()<
           material_draws_.size()+translucent_triangles.size())
            ordered_draws.reserve(
                material_draws_.size()+translucent_triangles.size());
        for(auto const& draw:material_draws_) {
            if(draw.blend_mode==2u)continue;
            auto updated=draw;
            updated.first=static_cast<std::uint32_t>(ordered.size());
            ordered.insert(ordered.end(),
                material_frame_vertices_.begin()+draw.first,
                material_frame_vertices_.begin()+draw.first+draw.count);
            ordered_draws.push_back(updated);
        }
        // Rebatch only adjacent sorted triangles with identical PSO/texture
        // bindings. Do not regroup distant fragments of the same material,
        // as that would break the global alpha-blending depth order.
        for(auto const& triangle:translucent_triangles) {
            auto const& source=material_draws_[triangle.draw_id];
            bool same=!ordered_draws.empty() &&
                ordered_draws.back().blend_mode==2u &&
                ordered_draws.back().pipeline==source.pipeline &&
                ordered_draws.back().textures==source.textures;
            if(!same) {
                auto next=source;
                next.first=static_cast<std::uint32_t>(ordered.size());
                next.count=0u;
                ordered_draws.push_back(next);
            }
            ordered.insert(ordered.end(),
                material_frame_vertices_.begin()+triangle.first,
                material_frame_vertices_.begin()+triangle.first+3u);
            ordered_draws.back().count+=3u;
        }
        material_frame_vertices_.swap(ordered);
        material_draws_.swap(ordered_draws);
    }
    auto const bytes=material_frame_vertices_.size()*sizeof(MaterialClipVertex);
    if(bytes>max_material_vertex_bytes_)
        throw std::runtime_error("Vulkan transformed material buffer overflow");
    if(bytes) {
        ensure_material_vertex_capacity(static_cast<VkDeviceSize>(bytes));
        void* mapped=nullptr;
        check(vkMapMemory(device_,material_vertices_memory_,0,bytes,0,&mapped),
              "vkMapMemory(material vertices)");
        std::memcpy(mapped,material_frame_vertices_.data(),bytes);
        diagnostics_.add(VulkanCounter::material_vertex_upload_bytes,bytes);
        frame_memory::copied(&NorthStarFrameMemoryCounters::geometry_copy_bytes,bytes);
        vkUnmapMemory(device_,material_vertices_memory_);
    }
}
} // namespace northstarcpp::render::vulkan
