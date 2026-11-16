// Native Vulkan 1.3 UI compositor, post-scene overlay pass.
// Embedded SPIR-V built from ui_overlay.vert/ui_overlay.frag (Vulkan SDK glslc).
#include "renderer_private.hpp"
#include "ui_overlay_spirv.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <iterator>

namespace northstarcpp::render::vulkan {
namespace {
void check_ui(VkResult value,const char* name) {
    if(value!=VK_SUCCESS)
        throw std::runtime_error(std::string(name)+": VkResult="+
                                 std::to_string(static_cast<int>(value)));
}
}
void Renderer::set_ui_fragment_spirv(const std::uint32_t* words,
                                    std::uint32_t count) {
    std::vector<std::uint32_t> next;
    if(words)next.assign(words,words+count);
    if(next==ui_fragment_spirv_)return;
    // The old pipeline may still be referenced by an in-flight submission.
    if(device_) {
        check_ui(vkDeviceWaitIdle(device_),"Wait for UI shader reload");
        destroy_ui_resources();
    }
    ui_fragment_spirv_.swap(next);
}
void Renderer::set_ui_rgba(const std::uint8_t* pixels,std::uint32_t width,
                            std::uint32_t height,std::uint32_t stride) {
    if(!pixels) {
        if(width||height||stride)
            throw std::invalid_argument("Invalid Vulkan UI clear");
        ui_cpu_rgba_.clear();ui_width_=ui_height_=0;ui_upload_dirty_=true;
        return;
    }
    if(!width||!height||width>8192||height>8192||
       static_cast<uint64_t>(width)*height*4>64ull*1024ull*1024ull ||
       stride<static_cast<uint64_t>(width)*4)
        throw std::invalid_argument("Invalid Vulkan UI image dimensions");
    std::vector<std::uint8_t> owned(static_cast<size_t>(width)*height*4);
    for(uint32_t y=0;y<height;++y)
        std::memcpy(owned.data()+static_cast<size_t>(y)*width*4,
                    pixels+static_cast<size_t>(y)*stride,
                    static_cast<size_t>(width)*4);
    ui_cpu_rgba_.swap(owned);ui_width_=width;ui_height_=height;
    ui_upload_dirty_=true;
}
void Renderer::destroy_ui_resources() noexcept {
    if(!device_)return;
    if(ui_pipeline_)vkDestroyPipeline(device_,ui_pipeline_,nullptr);
    if(ui_pipeline_layout_)vkDestroyPipelineLayout(device_,ui_pipeline_layout_,nullptr);
    if(ui_descriptor_pool_)vkDestroyDescriptorPool(device_,ui_descriptor_pool_,nullptr);
    if(ui_set_layout_)vkDestroyDescriptorSetLayout(device_,ui_set_layout_,nullptr);
    if(ui_sampler_)vkDestroySampler(device_,ui_sampler_,nullptr);
    if(ui_view_)vkDestroyImageView(device_,ui_view_,nullptr);
    if(ui_image_)vkDestroyImage(device_,ui_image_,nullptr);
    if(ui_image_memory_)vkFreeMemory(device_,ui_image_memory_,nullptr);
    if(ui_staging_)vkDestroyBuffer(device_,ui_staging_,nullptr);
    if(ui_staging_memory_)vkFreeMemory(device_,ui_staging_memory_,nullptr);
    ui_pipeline_=VK_NULL_HANDLE;ui_pipeline_layout_=VK_NULL_HANDLE;
    ui_descriptor_pool_=VK_NULL_HANDLE;ui_descriptor_set_=VK_NULL_HANDLE;
    ui_set_layout_=VK_NULL_HANDLE;ui_sampler_=VK_NULL_HANDLE;
    ui_view_=VK_NULL_HANDLE;ui_image_=VK_NULL_HANDLE;
    ui_image_memory_=VK_NULL_HANDLE;ui_staging_=VK_NULL_HANDLE;
    ui_staging_memory_=VK_NULL_HANDLE;
    ui_image_layout_=VK_IMAGE_LAYOUT_UNDEFINED;
    ui_upload_dirty_=true;
    ui_gpu_width_=ui_gpu_height_=0;
}
void Renderer::ensure_ui_resources() {
    if(ui_cpu_rgba_.empty())return;
    if(!ui_pipeline_) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding=0;binding.descriptorCount=1;
        binding.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo setinfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setinfo.bindingCount=1;setinfo.pBindings=&binding;
        check_ui(vkCreateDescriptorSetLayout(device_,&setinfo,nullptr,&ui_set_layout_),
                 "Create UI descriptor layout");
        VkDescriptorPoolSize poolSize{
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
        VkDescriptorPoolCreateInfo pool{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&poolSize;
        check_ui(vkCreateDescriptorPool(device_,&pool,nullptr,&ui_descriptor_pool_),
                 "Create UI descriptor pool");
        VkDescriptorSetAllocateInfo allocation{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool=ui_descriptor_pool_;
        allocation.descriptorSetCount=1;
        allocation.pSetLayouts=&ui_set_layout_;
        check_ui(vkAllocateDescriptorSets(device_,&allocation,&ui_descriptor_set_),
                 "Allocate UI descriptor");
        VkSamplerCreateInfo sampler{
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter=VK_FILTER_LINEAR;
        sampler.minFilter=VK_FILTER_LINEAR;
        sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod=0;
        check_ui(vkCreateSampler(device_,&sampler,nullptr,&ui_sampler_),
                 "Create UI sampler");
        VkPipelineLayoutCreateInfo layout{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount=1;layout.pSetLayouts=&ui_set_layout_;
        VkPushConstantRange hoverRange{};
        hoverRange.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        hoverRange.offset=0;
        hoverRange.size=sizeof(NorthStarRenderUiHoverFx);
        layout.pushConstantRangeCount=1;
        layout.pPushConstantRanges=&hoverRange;
        check_ui(vkCreatePipelineLayout(device_,&layout,nullptr,&ui_pipeline_layout_),
                 "Create UI pipeline layout");
        auto shader=[&](const uint32_t* source,size_t count) {
            VkShaderModule result=VK_NULL_HANDLE;
            VkShaderModuleCreateInfo desc{
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            desc.codeSize=count*sizeof(uint32_t);
            desc.pCode=source;
            check_ui(vkCreateShaderModule(device_,&desc,nullptr,&result),
                     "Create Vulkan UI shader");
            return result;
        };
        auto vertex=shader(shaders::ui_vert,std::size(shaders::ui_vert));
        auto fragment=ui_fragment_spirv_.empty()?
            shader(shaders::ui_frag,std::size(shaders::ui_frag)):
            shader(ui_fragment_spirv_.data(),ui_fragment_spirv_.size());
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module=vertex;stages[0].pName="main";
        stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module=fragment;stages[1].pName="main";
        VkPipelineVertexInputStateCreateInfo inputs{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
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
        raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable=VK_FALSE;depth.depthWriteEnable=VK_FALSE;
        VkPipelineColorBlendAttachmentState color{};
        color.blendEnable=VK_TRUE;
        color.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
        color.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color.colorBlendOp=VK_BLEND_OP_ADD;
        color.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
        color.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color.alphaBlendOp=VK_BLEND_OP_ADD;
        color.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                             VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount=1;blend.pAttachments=&color;
        VkDynamicState dynamicStates[]{
            VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamics{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamics.dynamicStateCount=2;dynamics.pDynamicStates=dynamicStates;
        VkPipelineRenderingCreateInfo rendering{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount=1;
        rendering.pColorAttachmentFormats=&format_;
        VkGraphicsPipelineCreateInfo desc{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        desc.pNext=&rendering;
        desc.stageCount=2;desc.pStages=stages;
        desc.pVertexInputState=&inputs;
        desc.pInputAssemblyState=&assembly;
        desc.pViewportState=&viewport;
        desc.pRasterizationState=&raster;
        desc.pMultisampleState=&multisample;
        desc.pDepthStencilState=&depth;
        desc.pColorBlendState=&blend;
        desc.pDynamicState=&dynamics;
        desc.layout=ui_pipeline_layout_;
        auto result=vkCreateGraphicsPipelines(
            device_,VK_NULL_HANDLE,1,&desc,nullptr,&ui_pipeline_);
        vkDestroyShaderModule(device_,vertex,nullptr);
        vkDestroyShaderModule(device_,fragment,nullptr);
        check_ui(result,"Create Vulkan UI graphics pipeline");
    }
    if(ui_image_&&ui_gpu_width_==ui_width_&&ui_gpu_height_==ui_height_)
        return;
    if(ui_view_)vkDestroyImageView(device_,ui_view_,nullptr);
    if(ui_image_)vkDestroyImage(device_,ui_image_,nullptr);
    if(ui_image_memory_)vkFreeMemory(device_,ui_image_memory_,nullptr);
    if(ui_staging_)vkDestroyBuffer(device_,ui_staging_,nullptr);
    if(ui_staging_memory_)vkFreeMemory(device_,ui_staging_memory_,nullptr);
    ui_image_=VK_NULL_HANDLE;ui_image_memory_=VK_NULL_HANDLE;
    ui_view_=VK_NULL_HANDLE;ui_staging_=VK_NULL_HANDLE;
    ui_staging_memory_=VK_NULL_HANDLE;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType=VK_IMAGE_TYPE_2D;
    image.format=VK_FORMAT_R8G8B8A8_UNORM;
    image.extent={ui_width_,ui_height_,1};
    image.mipLevels=1;image.arrayLayers=1;
    image.samples=VK_SAMPLE_COUNT_1_BIT;
    image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    check_ui(vkCreateImage(device_,&image,nullptr,&ui_image_),
             "Create Vulkan UI image");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_,ui_image_,&requirements);
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory.allocationSize=requirements.size;
    memory.memoryTypeIndex=find_memory_type(requirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check_ui(vkAllocateMemory(device_,&memory,nullptr,&ui_image_memory_),
             "Allocate Vulkan UI texture memory");
    check_ui(vkBindImageMemory(device_,ui_image_,ui_image_memory_,0),
             "Bind Vulkan UI texture memory");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image=ui_image_;view.viewType=VK_IMAGE_VIEW_TYPE_2D;
    view.format=image.format;
    view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount=1;view.subresourceRange.layerCount=1;
    check_ui(vkCreateImageView(device_,&view,nullptr,&ui_view_),
             "Create Vulkan UI texture view");
    VkBufferCreateInfo staging{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    staging.size=static_cast<VkDeviceSize>(ui_width_)*ui_height_*4;
    staging.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    check_ui(vkCreateBuffer(device_,&staging,nullptr,&ui_staging_),
             "Create Vulkan UI staging buffer");
    vkGetBufferMemoryRequirements(device_,ui_staging_,&requirements);
    memory.allocationSize=requirements.size;
    memory.memoryTypeIndex=find_memory_type(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check_ui(vkAllocateMemory(device_,&memory,nullptr,&ui_staging_memory_),
             "Allocate Vulkan UI staging memory");
    check_ui(vkBindBufferMemory(device_,ui_staging_,ui_staging_memory_,0),
             "Bind Vulkan UI staging memory");
    VkDescriptorImageInfo info{};
    info.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.imageView=ui_view_;info.sampler=ui_sampler_;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet=ui_descriptor_set_;write.dstBinding=0;
    write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount=1;write.pImageInfo=&info;
    vkUpdateDescriptorSets(device_,1,&write,0,nullptr);
    ui_image_layout_=VK_IMAGE_LAYOUT_UNDEFINED;
    ui_gpu_width_=ui_width_;ui_gpu_height_=ui_height_;
    ui_upload_dirty_=true;
}
void Renderer::record_ui_overlay(VkCommandBuffer command,std::uint32_t imageIndex) {
    if(ui_cpu_rgba_.empty()||!ui_image_||!ui_pipeline_)return;
    if(ui_upload_dirty_) {
    void* mapped=nullptr;
    check_ui(vkMapMemory(device_,ui_staging_memory_,0,ui_cpu_rgba_.size(),0,&mapped),
             "Map Vulkan UI staging");
    std::memcpy(mapped,ui_cpu_rgba_.data(),ui_cpu_rgba_.size());
    vkUnmapMemory(device_,ui_staging_memory_);
    VkImageMemoryBarrier2 toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toTransfer.srcStageMask=ui_image_layout_==VK_IMAGE_LAYOUT_UNDEFINED?
        VK_PIPELINE_STAGE_2_NONE:VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toTransfer.srcAccessMask=ui_image_layout_==VK_IMAGE_LAYOUT_UNDEFINED?
        VK_ACCESS_2_NONE:VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toTransfer.dstStageMask=VK_PIPELINE_STAGE_2_COPY_BIT;
    toTransfer.dstAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout=ui_image_layout_;
    toTransfer.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.image=ui_image_;
    toTransfer.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount=1;
    toTransfer.subresourceRange.layerCount=1;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount=1;
    dependency.pImageMemoryBarriers=&toTransfer;
    vkCmdPipelineBarrier2(command,&dependency);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount=1;
    copy.imageExtent={ui_width_,ui_height_,1};
    vkCmdCopyBufferToImage(command,ui_staging_,ui_image_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    VkImageMemoryBarrier2 toSample{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toSample.srcStageMask=VK_PIPELINE_STAGE_2_COPY_BIT;
    toSample.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toSample.dstStageMask=VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toSample.dstAccessMask=VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toSample.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toSample.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toSample.image=ui_image_;
    toSample.subresourceRange=toTransfer.subresourceRange;
    dependency.pImageMemoryBarriers=&toSample;
    vkCmdPipelineBarrier2(command,&dependency);
    ui_image_layout_=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ui_upload_dirty_=false;
    } // Reuse the GPU texture until UI pixels change.
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView=image_views_.at(imageIndex);
    color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent=extent_;
    info.layerCount=1;
    info.colorAttachmentCount=1;
    info.pColorAttachments=&color;
    vkCmdBeginRendering(command,&info);
    VkViewport viewport{0,0,static_cast<float>(extent_.width),
                                 static_cast<float>(extent_.height),0,1};
    VkRect2D scissors{};scissors.extent=extent_;
    vkCmdSetViewport(command,0,1,&viewport);
    vkCmdSetScissor(command,0,1,&scissors);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,ui_pipeline_);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ui_pipeline_layout_,0,1,&ui_descriptor_set_,0,nullptr);
    vkCmdPushConstants(command,ui_pipeline_layout_,
        VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(ui_hover_fx_),&ui_hover_fx_);
    vkCmdDraw(command,3,1,0,0);
    vkCmdEndRendering(command);
}
} // namespace northstarcpp::render::vulkan
