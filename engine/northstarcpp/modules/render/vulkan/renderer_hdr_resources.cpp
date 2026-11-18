// Owns the HDR scene and four separately sampled bloom levels.
// Resources are recreated only on swapchain resize, not on every frame.
#include "renderer_private.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
namespace northstarcpp::render::vulkan {
namespace {
void ensure_hdr(VkResult r,char const* name) {
    if(r!=VK_SUCCESS)throw std::runtime_error(std::string(name)+
        " VkResult="+std::to_string(int(r)));
}
}
void Renderer::destroy_hdr_resources() noexcept {
    if(!device_)return;
    if(luminance_pipeline_)vkDestroyPipeline(device_,luminance_pipeline_,nullptr);
    if(bloom_pipeline_)vkDestroyPipeline(device_,bloom_pipeline_,nullptr);
    if(composite_pipeline_)vkDestroyPipeline(device_,composite_pipeline_,nullptr);
    if(bloom_pipeline_layout_)
        vkDestroyPipelineLayout(device_,bloom_pipeline_layout_,nullptr);
    if(composite_pipeline_layout_)
        vkDestroyPipelineLayout(device_,composite_pipeline_layout_,nullptr);
    if(hdr_descriptor_pool_)
        vkDestroyDescriptorPool(device_,hdr_descriptor_pool_,nullptr);
    if(bloom_set_layout_)
        vkDestroyDescriptorSetLayout(device_,bloom_set_layout_,nullptr);
    if(composite_set_layout_)
        vkDestroyDescriptorSetLayout(device_,composite_set_layout_,nullptr);
    if(hdr_sampler_)vkDestroySampler(device_,hdr_sampler_,nullptr);
    if(luminance_readback_buffer_)vkDestroyBuffer(device_,luminance_readback_buffer_,nullptr);
    if(luminance_readback_memory_)vkFreeMemory(device_,luminance_readback_memory_,nullptr);
    if(luminance_view_)vkDestroyImageView(device_,luminance_view_,nullptr);
    if(luminance_image_)vkDestroyImage(device_,luminance_image_,nullptr);
    if(luminance_memory_)vkFreeMemory(device_,luminance_memory_,nullptr);
    for(unsigned i=0;i<bloom_levels_;++i) {
        if(bloom_views_[i])vkDestroyImageView(device_,bloom_views_[i],nullptr);
        if(bloom_images_[i])vkDestroyImage(device_,bloom_images_[i],nullptr);
        if(bloom_memories_[i])vkFreeMemory(device_,bloom_memories_[i],nullptr);
    }
    if(velocity_view_)vkDestroyImageView(device_,velocity_view_,nullptr);
    if(velocity_image_)vkDestroyImage(device_,velocity_image_,nullptr);
    if(velocity_memory_)vkFreeMemory(device_,velocity_memory_,nullptr);
    velocity_view_=VK_NULL_HANDLE;
    velocity_image_=VK_NULL_HANDLE;
    velocity_memory_=VK_NULL_HANDLE;
    if(hdr_scene_view_)vkDestroyImageView(device_,hdr_scene_view_,nullptr);
    if(hdr_scene_image_)vkDestroyImage(device_,hdr_scene_image_,nullptr);
    if(hdr_scene_memory_)vkFreeMemory(device_,hdr_scene_memory_,nullptr);
    bloom_pipeline_=composite_pipeline_=VK_NULL_HANDLE;
    bloom_pipeline_layout_=composite_pipeline_layout_=VK_NULL_HANDLE;
    hdr_descriptor_pool_=VK_NULL_HANDLE;
    bloom_set_layout_=composite_set_layout_=VK_NULL_HANDLE;
    hdr_sampler_=VK_NULL_HANDLE;
    hdr_scene_image_=VK_NULL_HANDLE;hdr_scene_memory_=VK_NULL_HANDLE;
    hdr_scene_view_=VK_NULL_HANDLE;
    bloom_images_={};bloom_memories_={};bloom_views_={};
    bloom_extents_={};bloom_descriptor_sets_={};
    composite_descriptor_set_=VK_NULL_HANDLE;
    luminance_descriptor_set_=VK_NULL_HANDLE;
    luminance_pipeline_=VK_NULL_HANDLE;
    luminance_image_=VK_NULL_HANDLE;luminance_memory_=VK_NULL_HANDLE;
    luminance_view_=VK_NULL_HANDLE;
    luminance_readback_buffer_=VK_NULL_HANDLE;
    luminance_readback_memory_=VK_NULL_HANDLE;
    exposure_pending_=false;
}
void Renderer::create_hdr_resources() {
    if(!hdr_enabled_)return;
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physical_,hdr_format_,&props);
    constexpr auto required=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if((props.optimalTilingFeatures&required)!=required)
        throw std::runtime_error("Vulkan HDR requires linear-filterable RGBA16F attachments");
    auto allocate=[&](VkExtent2D size,VkImage& image,
                      VkDeviceMemory& memory,VkImageView& view,
                      VkFormat image_format=hdr_format_,
                      VkImageUsageFlags extra_usage=0u) {
        VkImageCreateInfo desc{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        desc.imageType=VK_IMAGE_TYPE_2D;
        desc.format=image_format;
        desc.extent={size.width,size.height,1};
        desc.mipLevels=1;desc.arrayLayers=1;
        desc.samples=VK_SAMPLE_COUNT_1_BIT;
        desc.tiling=VK_IMAGE_TILING_OPTIMAL;
        desc.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|
                   VK_IMAGE_USAGE_SAMPLED_BIT|extra_usage;
        ensure_hdr(vkCreateImage(device_,&desc,nullptr,&image),
                   "vkCreateImage(HDR)");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_,image,&req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize=req.size;
        alloc.memoryTypeIndex=find_memory_type(
            req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ensure_hdr(vkAllocateMemory(device_,&alloc,nullptr,&memory),
                   "vkAllocateMemory(HDR)");
        ensure_hdr(vkBindImageMemory(device_,image,memory,0),
                   "vkBindImageMemory(HDR)");
        VkImageViewCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        create.image=image;create.viewType=VK_IMAGE_VIEW_TYPE_2D;
        create.format=image_format;
        create.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        create.subresourceRange.levelCount=1;
        create.subresourceRange.layerCount=1;
        ensure_hdr(vkCreateImageView(device_,&create,nullptr,&view),
                   "vkCreateImageView(HDR)");
    };
    allocate(extent_,hdr_scene_image_,hdr_scene_memory_,hdr_scene_view_);
    if(velocity_enabled_) {
        VkFormatProperties velocity_features{};
        vkGetPhysicalDeviceFormatProperties(
            physical_,VK_FORMAT_R16G16_SFLOAT,&velocity_features);
        constexpr VkFormatFeatureFlags required_features=
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if((velocity_features.optimalTilingFeatures&required_features)!=
           required_features)
            throw std::runtime_error("Vulkan RG16F motion vectors unsupported");
        allocate(extent_,velocity_image_,velocity_memory_,velocity_view_,
                 VK_FORMAT_R16G16_SFLOAT);
    }
    VkExtent2D size=extent_;
    for(unsigned i=0;i<bloom_levels_;++i) {
        size.width=std::max(1u,(size.width+1)/2);
        size.height=std::max(1u,(size.height+1)/2);
        bloom_extents_[i]=size;
        allocate(size,bloom_images_[i],bloom_memories_[i],bloom_views_[i]);
    }
    if(auto_exposure_enabled_) {
        constexpr VkFormat meter_format=VK_FORMAT_R16G16_SFLOAT;
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical_,meter_format,&properties);
        if(!(properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
            throw std::runtime_error("Luminance RG16F attachment unsupported");
        allocate({luminance_dim_,luminance_dim_},luminance_image_,
            luminance_memory_,luminance_view_,meter_format,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size=luminance_dim_*luminance_dim_*4u;
        info.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        ensure_hdr(vkCreateBuffer(device_,&info,nullptr,
                   &luminance_readback_buffer_),"vkCreateBuffer(Luminance)");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_,luminance_readback_buffer_,
                                      &requirements);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        memory.allocationSize=requirements.size;
        memory.memoryTypeIndex=find_memory_type(requirements.memoryTypeBits,
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ensure_hdr(vkAllocateMemory(device_,&memory,nullptr,
                   &luminance_readback_memory_),"vkAllocateMemory(Luminance)");
        ensure_hdr(vkBindBufferMemory(device_,luminance_readback_buffer_,
                   luminance_readback_memory_,0),"vkBindBufferMemory(Luminance)");
    }
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
    sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ensure_hdr(vkCreateSampler(device_,&sampler,nullptr,&hdr_sampler_),
               "vkCreateSampler(HDR)");
    VkDescriptorSetLayoutBinding first{};
    first.binding=0;first.descriptorCount=1;
    first.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    first.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount=1;layout.pBindings=&first;
    ensure_hdr(vkCreateDescriptorSetLayout(device_,&layout,nullptr,
               &bloom_set_layout_),"vkCreateDescriptorSetLayout(Bloom)");
    std::array<VkDescriptorSetLayoutBinding,7> bindings{};
    for(unsigned i=0;i<bindings.size();++i) {
        bindings[i]=first;bindings[i].binding=i;
    }
    layout.bindingCount=static_cast<std::uint32_t>(bindings.size());
    layout.pBindings=bindings.data();
    ensure_hdr(vkCreateDescriptorSetLayout(device_,&layout,nullptr,
               &composite_set_layout_),"vkCreateDescriptorSetLayout(Composite)");
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    bloom_levels_+7+(auto_exposure_enabled_?1u:0u)};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=bloom_levels_+1+(auto_exposure_enabled_?1u:0u);
    pool.poolSizeCount=1;pool.pPoolSizes=&pool_size;
    ensure_hdr(vkCreateDescriptorPool(device_,&pool,nullptr,
               &hdr_descriptor_pool_),"vkCreateDescriptorPool(HDR)");
    for(unsigned i=0;i<bloom_levels_;++i) {
        VkDescriptorSetAllocateInfo alloc{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool=hdr_descriptor_pool_;
        alloc.descriptorSetCount=1;alloc.pSetLayouts=&bloom_set_layout_;
        ensure_hdr(vkAllocateDescriptorSets(device_,&alloc,
                   &bloom_descriptor_sets_[i]),
                   "vkAllocateDescriptorSets(Bloom)");
        VkDescriptorImageInfo source{
            hdr_sampler_,i?bloom_views_[i-1]:hdr_scene_view_,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet=bloom_descriptor_sets_[i];
        write.dstBinding=0;write.descriptorCount=1;
        write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo=&source;
        vkUpdateDescriptorSets(device_,1,&write,0,nullptr);
    }
    if(auto_exposure_enabled_) {
        VkDescriptorSetAllocateInfo lum_alloc{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        lum_alloc.descriptorPool=hdr_descriptor_pool_;
        lum_alloc.descriptorSetCount=1;
        lum_alloc.pSetLayouts=&bloom_set_layout_;
        ensure_hdr(vkAllocateDescriptorSets(device_,&lum_alloc,
            &luminance_descriptor_set_),"vkAllocateDescriptorSets(Luminance)");
        VkDescriptorImageInfo source{
            hdr_sampler_,hdr_scene_view_,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet=luminance_descriptor_set_;
        write.descriptorCount=1;
        write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo=&source;
        vkUpdateDescriptorSets(device_,1,&write,0,nullptr);
    }
    VkDescriptorSetAllocateInfo alloc{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool=hdr_descriptor_pool_;
    alloc.descriptorSetCount=1;alloc.pSetLayouts=&composite_set_layout_;
    ensure_hdr(vkAllocateDescriptorSets(device_,&alloc,
               &composite_descriptor_set_),
               "vkAllocateDescriptorSets(Composite)");
    std::array<VkDescriptorImageInfo,7> sources{};
    std::array<VkWriteDescriptorSet,7> writes{};
    for(unsigned i=0;i<7;++i) {
        const auto view=i==6u?
            (velocity_enabled_?velocity_view_:hdr_scene_view_):
            i==5u?
            (auto_exposure_enabled_?luminance_view_:hdr_scene_view_):
            (i && (i-1)<active_bloom_levels_?
                bloom_views_[i-1]:hdr_scene_view_);
        sources[i]={hdr_sampler_,view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet=composite_descriptor_set_;
        writes[i].dstBinding=i;writes[i].descriptorCount=1;
        writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo=&sources[i];
    }
    vkUpdateDescriptorSets(device_,static_cast<std::uint32_t>(writes.size()),
                           writes.data(),0,nullptr);
}
}
