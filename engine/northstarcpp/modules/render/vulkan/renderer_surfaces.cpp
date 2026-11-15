// Vulkan native BC1/BC5 world-surface provider. No decompression to
// substitute colors: compressed original mip chains are uploaded as VkImages.
#include "renderer_private.hpp"
#include "material_spirv.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace northstarcpp::render::vulkan {
namespace {
void check(VkResult result,const char* name) {
    if(result!=VK_SUCCESS)
        throw std::runtime_error(std::string(name)+" failed: VkResult="+
                                 std::to_string(static_cast<int>(result)));
}
void destroy_texture(VkDevice device,MaterialTextureGpu& texture) noexcept {
    if(texture.view)vkDestroyImageView(device,texture.view,nullptr);
    if(texture.image)vkDestroyImage(device,texture.image,nullptr);
    if(texture.memory)vkFreeMemory(device,texture.memory,nullptr);
    texture={};
}
VkFormat surface_format(const NorthStarSurfaceTexture& desc) {
    if(desc.format_id==102)
        return desc.srgb?VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
                         VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    if(desc.format_id==105 && !desc.srgb)
        return VK_FORMAT_BC5_UNORM_BLOCK;
    throw std::invalid_argument("Vulkan world surface must be BC1 or linear BC5");
}
}
void Renderer::set_cube_surfaces(const std::uint32_t* ids,std::uint32_t count) {
    if(count>200000||(count&&!ids))
        throw std::invalid_argument("Vulkan surface selector count/pointer invalid");
    std::vector<std::uint32_t> next;
    next.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        if(ids[i]>3)
            throw std::invalid_argument("Vulkan cube surface selector must be 0..3");
        next.push_back(ids[i]);
    }
    world_surface_ids_=std::move(next);
}
void Renderer::create_world_resources() {
    std::array<VkDescriptorSetLayoutBinding,9> bindings{};
    for(std::uint32_t i=0;i<9;++i) {
        bindings[i].binding=i;
        bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount=1;
        bindings[i].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount=9;layout.pBindings=bindings.data();
    check(vkCreateDescriptorSetLayout(device_,&layout,nullptr,&world_set_layout_),
          "vkCreateDescriptorSetLayout(world surfaces)");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,9};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&size;
    check(vkCreateDescriptorPool(device_,&pool,nullptr,&world_descriptor_pool_),
          "vkCreateDescriptorPool(world surfaces)");
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool=world_descriptor_pool_;
    allocation.descriptorSetCount=1;
    allocation.pSetLayouts=&world_set_layout_;
    check(vkAllocateDescriptorSets(device_,&allocation,&world_set_),
          "vkAllocateDescriptorSets(world surfaces)");
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter=VK_FILTER_LINEAR;
    sampler.minFilter=VK_FILTER_LINEAR;
    sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=
        VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod=14.f;
    check(vkCreateSampler(device_,&sampler,nullptr,&world_sampler_),
          "vkCreateSampler(world surfaces)");
}
void Renderer::destroy_world_resources() noexcept {
    if(!device_)return;
    for(auto& texture:world_textures_)destroy_texture(device_,texture);
    world_texture_resident_.fill(false);
    if(world_sampler_)vkDestroySampler(device_,world_sampler_,nullptr);
    if(world_descriptor_pool_)
        vkDestroyDescriptorPool(device_,world_descriptor_pool_,nullptr);
    if(world_set_layout_)
        vkDestroyDescriptorSetLayout(device_,world_set_layout_,nullptr);
    world_sampler_=VK_NULL_HANDLE;
    world_descriptor_pool_=VK_NULL_HANDLE;
    world_set_layout_=VK_NULL_HANDLE;
    world_set_=VK_NULL_HANDLE;
    world_surface_ids_.clear();
}
void Renderer::set_world_texture(const NorthStarSurfaceTexture& source) {
    if(source.slot>=9||!source.data||!source.mips||
       source.mip_count==0||source.mip_count>15 ||
       source.data_bytes==0||source.data_bytes>32u*1024u*1024u ||
       source.srgb>1)
        throw std::invalid_argument("Invalid native Vulkan BC surface descriptor");
    const auto format=surface_format(source);
    if(source.slot%3==0) {
        if(source.format_id!=102||!source.srgb)
            throw std::invalid_argument("World surface albedo must be sRGB BC1");
    }else if(source.format_id!=105||source.srgb)
        throw std::invalid_argument("World normal/roughness must be linear BC5");
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physical_,format,&props);
    if(!(props.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        throw std::runtime_error("Physical GPU lacks required BCn sampled format");
    auto w=source.mips[0].width,h=source.mips[0].height;
    if(!w||!h||w>16384||h>16384)
        throw std::invalid_argument("World BC surface dimensions invalid");
    for(std::uint32_t level=0;level<source.mip_count;++level) {
        const auto& mip=source.mips[level];
        const std::uint64_t expected=
            std::uint64_t((w+3u)/4u)*((h+3u)/4u)*(source.format_id==102?8u:16u);
        if(mip.width!=w||mip.height!=h||
           mip.bytes!=expected||mip.offset>source.data_bytes||
           expected>source.data_bytes-mip.offset)
            throw std::invalid_argument("World compressed mip chain is invalid");
        w=std::max(1u,w/2u);h=std::max(1u,h/2u);
    }
    // Only touch the current live texture after a COMPLETE GPU transfer.
    check(vkDeviceWaitIdle(device_),"vkDeviceWaitIdle(world texture replace)");
    MaterialTextureGpu next{};
    VkBuffer staging=VK_NULL_HANDLE;
    VkDeviceMemory staging_memory=VK_NULL_HANDLE;
    VkCommandBuffer command=VK_NULL_HANDLE;
    auto cleanup=[&] {
        if(command)vkFreeCommandBuffers(device_,command_pool_,1,&command);
        if(staging)vkDestroyBuffer(device_,staging,nullptr);
        if(staging_memory)vkFreeMemory(device_,staging_memory,nullptr);
    };
    try {
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType=VK_IMAGE_TYPE_2D;
        image.format=format;
        image.extent={source.mips[0].width,source.mips[0].height,1};
        image.mipLevels=source.mip_count;
        image.arrayLayers=1;
        image.samples=VK_SAMPLE_COUNT_1_BIT;
        image.tiling=VK_IMAGE_TILING_OPTIMAL;
        image.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        image.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device_,&image,nullptr,&next.image),
              "vkCreateImage(world BC texture)");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_,next.image,&req);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        memory.allocationSize=req.size;
        memory.memoryTypeIndex=find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_,&memory,nullptr,&next.memory),
              "vkAllocateMemory(world BC texture)");
        check(vkBindImageMemory(device_,next.image,next.memory,0),
              "vkBindImageMemory(world BC texture)");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image=next.image;
        view.viewType=VK_IMAGE_VIEW_TYPE_2D;
        view.format=format;
        view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount=source.mip_count;
        view.subresourceRange.layerCount=1;
        check(vkCreateImageView(device_,&view,nullptr,&next.view),
              "vkCreateImageView(world BC texture)");

        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size=source.data_bytes;
        buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        check(vkCreateBuffer(device_,&buffer,nullptr,&staging),
              "vkCreateBuffer(world staging)");
        vkGetBufferMemoryRequirements(device_,staging,&req);
        memory.allocationSize=req.size;
        memory.memoryTypeIndex=find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device_,&memory,nullptr,&staging_memory),
              "vkAllocateMemory(world staging)");
        check(vkBindBufferMemory(device_,staging,staging_memory,0),
              "vkBindBufferMemory(world staging)");
        void* mapped=nullptr;
        check(vkMapMemory(device_,staging_memory,0,source.data_bytes,0,&mapped),
              "vkMapMemory(world staging)");
        std::memcpy(mapped,source.data,source.data_bytes);
        vkUnmapMemory(device_,staging_memory);
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool=command_pool_;
        alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount=1;
        check(vkAllocateCommandBuffers(device_,&alloc,&command),
              "vkAllocateCommandBuffers(world BC)");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command,&begin),"vkBeginCommandBuffer(world)");
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask=VK_PIPELINE_STAGE_2_NONE;
        barrier.dstStageMask=VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.dstAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=next.image;
        barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount=source.mip_count;
        barrier.subresourceRange.layerCount=1;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount=1;
        dependency.pImageMemoryBarriers=&barrier;
        vkCmdPipelineBarrier2(command,&dependency);
        std::vector<VkBufferImageCopy> copies;
        copies.reserve(source.mip_count);
        for(std::uint32_t i=0;i<source.mip_count;++i) {
            const auto& mip=source.mips[i];
            VkBufferImageCopy copy{};
            copy.bufferOffset=mip.offset;
            copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel=i;
            copy.imageSubresource.layerCount=1;
            copy.imageExtent={mip.width,mip.height,1};
            copies.push_back(copy);
        }
        vkCmdCopyBufferToImage(command,staging,next.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<std::uint32_t>(copies.size()),copies.data());
        barrier.srcStageMask=VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask=VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask=VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier2(command,&dependency);
        check(vkEndCommandBuffer(command),"vkEndCommandBuffer(world)");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount=1;
        submit.pCommandBuffers=&command;
        check(vkQueueSubmit(queue_,1,&submit,VK_NULL_HANDLE),
              "vkQueueSubmit(world BC upload)");
        check(vkQueueWaitIdle(queue_),"vkQueueWaitIdle(world BC upload)");
        cleanup();
        command=VK_NULL_HANDLE;
        staging=VK_NULL_HANDLE;
        staging_memory=VK_NULL_HANDLE;
    }catch(...) {
        vkQueueWaitIdle(queue_);
        cleanup();
        destroy_texture(device_,next);
        throw;
    }
    VkDescriptorImageInfo image_info{
        world_sampler_,next.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet=world_set_;
    write.dstBinding=source.slot;
    write.descriptorCount=1;
    write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo=&image_info;
    vkUpdateDescriptorSets(device_,1,&write,0,nullptr);
    destroy_texture(device_,world_textures_[source.slot]);
    world_textures_[source.slot]=std::move(next);
    world_texture_resident_[source.slot]=true;
}
void Renderer::create_world_pipeline() {
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize=sizeof(shaders::world_vertex);
    shader.pCode=shaders::world_vertex;
    check(vkCreateShaderModule(device_,&shader,nullptr,&world_vs_),
          "vkCreateShaderModule(world VS)");
    shader.codeSize=sizeof(shaders::world_fragment);
    shader.pCode=shaders::world_fragment;
    check(vkCreateShaderModule(device_,&shader,nullptr,&world_fs_),
          "vkCreateShaderModule(world FS)");
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount=1;
    layout.pSetLayouts=&world_set_layout_;
    check(vkCreatePipelineLayout(device_,&layout,nullptr,&world_pipeline_layout_),
          "vkCreatePipelineLayout(world)");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(unsigned i=0;i<2;++i) {
        stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module=i?world_fs_:world_vs_;
        stages[i].pName="main";
    }
    VkVertexInputBindingDescription binding{0,sizeof(ColoredVertex),VK_VERTEX_INPUT_RATE_VERTEX};
    std::array<VkVertexInputAttributeDescription,4> attrs{{
        {0,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(ColoredVertex,clip)},
        {1,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(ColoredVertex,color)},
        {2,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(ColoredVertex,world)},
        {3,0,VK_FORMAT_R32_SFLOAT,offsetof(ColoredVertex,surface)}
    }};
    VkPipelineVertexInputStateCreateInfo vertex{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;
    vertex.vertexAttributeDescriptionCount=4;vertex.pVertexAttributeDescriptions=attrs.data();
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
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable=VK_TRUE;depth.depthWriteEnable=VK_TRUE;
    depth.depthCompareOp=VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                              VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount=1;blend.pAttachments=&attachment;
    VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamics{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamics.dynamicStateCount=2;dynamics.pDynamicStates=dynamic_states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount=1;
    rendering.pColorAttachmentFormats=&format_;
    rendering.depthAttachmentFormat=depth_format_;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.pNext=&rendering;
    pipeline.stageCount=2;pipeline.pStages=stages;
    pipeline.pVertexInputState=&vertex;
    pipeline.pInputAssemblyState=&assembly;
    pipeline.pViewportState=&viewport;
    pipeline.pRasterizationState=&raster;
    pipeline.pMultisampleState=&ms;
    pipeline.pDepthStencilState=&depth;
    pipeline.pColorBlendState=&blend;
    pipeline.pDynamicState=&dynamics;
    pipeline.layout=world_pipeline_layout_;
    check(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&pipeline,nullptr,
                                    &world_pipeline_),
          "vkCreateGraphicsPipelines(world compressed textures)");
}
} // namespace northstarcpp::render::vulkan
