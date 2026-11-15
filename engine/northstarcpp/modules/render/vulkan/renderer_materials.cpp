// Vulkan material ABI v1. Native GPU texture sampling and GGX PBR.
// CPU copies of all host-owned vertices/texels, bounded and lazy GPU uploads.
#include "renderer_private.hpp"
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

namespace northstarcpp::render::vulkan {
namespace {
void check(VkResult result,const char* name) {
    if(result!=VK_SUCCESS)
        throw std::runtime_error(std::string(name)+" failed: VkResult="+
                                 std::to_string(static_cast<int>(result)));
}
std::uint64_t hash_texture(const std::vector<std::uint8_t>& bytes,
                           std::uint32_t width,std::uint32_t height,VkFormat format) {
    std::uint64_t hash=14695981039346656037ull;
    auto add=[&](std::uint8_t n) {hash^=n;hash*=1099511628211ull;};
    for(unsigned i=0;i<4;++i) {
        add(static_cast<std::uint8_t>(width>>(i*8)));
        add(static_cast<std::uint8_t>(height>>(i*8)));
        add(static_cast<std::uint8_t>(static_cast<unsigned>(format)>>(i*8)));
    }
    for(auto byte:bytes)add(byte);
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
    next.reserve(count);
    std::uint64_t total_vertices=0,total_texels=0;
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
                continue;
            }
            if(!image.width||!image.height||image.width>4096||image.height>4096||
               static_cast<std::uint64_t>(image.width)*image.height*4!=image.data_bytes)
                throw std::invalid_argument("Malformed Vulkan RGBA8 material texture");
            total_texels+=image.data_bytes;
            if(total_texels>256ull*1024*1024)
                throw std::invalid_argument("Vulkan material texture budget exceeded");
            source.widths[k]=image.width;source.heights[k]=image.height;
            source.textures[k].assign(image.rgba8,image.rgba8+image.data_bytes);
        }
        for(std::uint32_t k=0;k<input.vertex_count;++k) {
            auto const& vertex=input.vertices[k];
            if(!std::isfinite(vertex.x)||!std::isfinite(vertex.y)||
               !std::isfinite(vertex.z)||!std::isfinite(vertex.nx)||
               !std::isfinite(vertex.ny)||!std::isfinite(vertex.nz)||
               !std::isfinite(vertex.u)||!std::isfinite(vertex.v))
                throw std::invalid_argument("Vulkan material geometry contains NaN/Inf");
        }
        source.vertices.assign(input.vertices,input.vertices+input.vertex_count);
        next.push_back(std::move(source));
    }
    bool textures_same=next.size()==material_sources_.size();
    if(textures_same)for(std::size_t i=0;i<next.size()&&textures_same;++i) {
        for(unsigned slot=0;slot<5;++slot) {
            if(next[i].widths[slot]!=material_sources_[i].widths[slot] ||
               next[i].heights[slot]!=material_sources_[i].heights[slot] ||
               next[i].textures[slot]!=material_sources_[i].textures[slot]) {
                textures_same=false;break;
            }
        }
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
    base.reserve(count);programs.reserve(count);
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
void Renderer::create_material_resources() {
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size=max_material_vertex_bytes_;
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

    std::array<VkDescriptorSetLayoutBinding,6> bindings{};
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
    VkDescriptorPoolSize sizes[2]{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            5u*NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES;
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
    std::vector<std::uint8_t> raw=values.empty()?
        std::vector<std::uint8_t>(defaults.begin(),defaults.end()):values;
    auto const width=values.empty()?1u:source.widths[slot];
    auto const height=values.empty()?1u:source.heights[slot];
    auto const format=(slot==0||slot==4)?
        VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;

    MaterialTextureGpu texture;
    texture.width=width;texture.height=height;texture.format=format;
    texture.source_bytes=raw;
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
        check(vkQueueWaitIdle(queue_),"vkQueueWaitIdle(material upload)");
        cleanup_temp();
        return texture;
    }catch(...) {
        // Submission failures still demand a best-effort idle before freeing
        // resources potentially referenced by a queued copy command.
        vkQueueWaitIdle(queue_);
        cleanup_temp();
        if(texture.view)vkDestroyImageView(device_,texture.view,nullptr);
        if(texture.image)vkDestroyImage(device_,texture.image,nullptr);
        if(texture.memory)vkFreeMemory(device_,texture.memory,nullptr);
        throw;
    }
}
void Renderer::upload_material_textures() {
    if(!material_textures_dirty_)return;
    check(vkDeviceWaitIdle(device_),"vkDeviceWaitIdle(material refresh)");
    reset_material_textures();
    const auto total=material_sources_.size()+skinned_sources_.size();
    if(total>NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES)
        throw std::runtime_error("Combined Vulkan authored + GPU skinned batch budget exceeded");
    material_sets_.resize(total,VK_NULL_HANDLE);
    for(std::size_t batch_id=0;batch_id<total;++batch_id) {
        auto const& source=batch_id<material_sources_.size()?
            material_sources_[batch_id]:
            skinned_sources_[batch_id-material_sources_.size()].material;
        std::array<VkDescriptorImageInfo,5> images{};
        for(std::uint32_t slot=0;slot<5;++slot) {
            auto const& bytes=source.textures[slot];
            auto const defaults=fallback_rgba(slot);
            auto const raw=bytes.empty()?
                std::vector<std::uint8_t>(defaults.begin(),defaults.end()):bytes;
            auto const width=bytes.empty()?1u:source.widths[slot];
            auto const height=bytes.empty()?1u:source.heights[slot];
            auto const format=(slot==0||slot==4)?
                VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;
            auto key=hash_texture(raw,width,height,format);
            while(true) {
                auto it=material_texture_cache_.find(key);
                if(it==material_texture_cache_.end()) {
                    auto image=upload_material_texture(source,slot);
                    it=material_texture_cache_.emplace(key,std::move(image)).first;
                }
                if(it->second.width==width&&it->second.height==height &&
                   it->second.format==format&&it->second.source_bytes==raw) {
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
}
VkPipeline Renderer::create_pipeline_for_material_fragment(
    VkShaderModule fragment,std::uint32_t blend_mode) {
    if(!fragment||blend_mode>2)
        throw std::invalid_argument("Invalid Vulkan material pipeline variant");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(unsigned i=0;i<2;++i) {
        stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module=i?fragment:material_vertex_shader_;
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
    VkPipelineColorBlendAttachmentState write{};
    write.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                         VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    if(blend_mode==2) {
        write.blendEnable=VK_TRUE;
        write.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
        write.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        write.colorBlendOp=VK_BLEND_OP_ADD;
        write.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
        write.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        write.alphaBlendOp=VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount=1;blend.pAttachments=&write;
    VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamic_states;
    VkPipelineRenderingCreateInfo rendering{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount=1;
    rendering.pColorAttachmentFormats=&format_;
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
    check(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&pipeline,
                                    nullptr,&created),
          "vkCreateGraphicsPipelines(material)");
    return created;
}
VkPipeline Renderer::material_graph_pipeline(
    const std::string& prefix,std::uint32_t blend_mode) {
    if(prefix.empty() && blend_mode==0)return material_pipeline_;
    auto key=std::to_string(blend_mode)+"|"+prefix;
    if(auto found=material_variant_pipelines_.find(key);
       found!=material_variant_pipelines_.end())return found->second;
    if(material_variant_pipelines_.size()>=128)
        throw std::runtime_error("Vulkan material graph PSO budget exceeded");
    std::string glsl(shaders::material_fragment_glsl);
    auto const directive=glsl.find('\n');
    if(directive==std::string::npos || glsl.rfind("#version",0)!=0)
        throw std::logic_error("Invalid embedded Vulkan Material Graph GLSL");
    glsl.insert(directive+1,prefix);
    auto bytecode=compile_material_fragment_spirv(glsl);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize=bytecode.size()*sizeof(std::uint32_t);
    ci.pCode=bytecode.data();
    VkShaderModule module=VK_NULL_HANDLE;
    check(vkCreateShaderModule(device_,&ci,nullptr,&module),
          "vkCreateShaderModule(Material Graph SPIR-V)");
    try {
        auto pipeline=create_pipeline_for_material_fragment(module,blend_mode);
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
        MaterialDraw draw{};
        draw.first=static_cast<std::uint32_t>(material_frame_vertices_.size());
        draw.textures=material_sets_.at(index);
        draw.blend_mode=batch.blend_mode;
        draw.pipeline=material_graph_pipeline(
            batch.variant_prefix,batch.blend_mode);
        for(std::size_t i=0;i+2<batch.vertices.size();i+=3) {
            MaterialClipVertex tri[3]{};
            bool visible=true;
            for(unsigned j=0;j<3;++j) {
                auto const& v=batch.vertices[i+j];
                auto const dx=v.x-frame.camera_x;
                auto const dy=v.y-frame.camera_y;
                auto const dz=v.z-frame.camera_z;
                auto const horizontal=dx*cy-dz*sy;
                auto const forward=dx*sy+dz*cy;
                auto const vertical=dy*cp-forward*sp;
                auto const distance=dy*sp+forward*cp;
                visible=visible&&distance>near_clip;
                auto& out=tri[j];
                out.clip[0]=horizontal*factor/aspect;
                out.clip[1]=-vertical*factor;
                out.clip[2]=(far_clip*distance-far_clip*near_clip)/(far_clip-near_clip);
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
            if(visible)
                material_frame_vertices_.insert(material_frame_vertices_.end(),
                                                 tri,tri+3);
        }
        draw.count=static_cast<std::uint32_t>(material_frame_vertices_.size())-
                   draw.first;
        if(draw.count)material_draws_.push_back(draw);
    }
    auto const bytes=material_frame_vertices_.size()*sizeof(MaterialClipVertex);
    if(bytes>max_material_vertex_bytes_)
        throw std::runtime_error("Vulkan transformed material buffer overflow");
    if(bytes) {
        void* mapped=nullptr;
        check(vkMapMemory(device_,material_vertices_memory_,0,bytes,0,&mapped),
              "vkMapMemory(material vertices)");
        std::memcpy(mapped,material_frame_vertices_.data(),bytes);
        vkUnmapMemory(device_,material_vertices_memory_);
    }
}
} // namespace northstarcpp::render::vulkan
