// Native Vulkan 1.3 deferred MRT framebuffer and sampled resolve.
// Separate provider module: C engine.render ABI and forward path are unchanged.
#include "renderer_private.hpp"
#include "deferred_spirv.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
namespace northstarcpp::render::vulkan {
namespace {
void ensure(VkResult code,const char* op) {
    if(code!=VK_SUCCESS)throw std::runtime_error(
        std::string(op)+" failed: VkResult="+std::to_string(int(code)));
}
VkShaderModule shader(VkDevice d,const std::uint32_t* code,std::size_t bytes) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize=bytes;info.pCode=code;
    VkShaderModule m=VK_NULL_HANDLE;
    ensure(vkCreateShaderModule(d,&info,nullptr,&m),"Create deferred shader");
    return m;
}
}
void Renderer::destroy_deferred_resources() noexcept {
    if(!device_)return;
    if(velocity_pipeline_)
        vkDestroyPipeline(device_,velocity_pipeline_,nullptr);
    velocity_pipeline_=VK_NULL_HANDLE;
    if(ssao_pipeline_)vkDestroyPipeline(device_,ssao_pipeline_,nullptr);
    ssao_pipeline_=VK_NULL_HANDLE;
    destroy_ssao_resource();
    destroy_csm_resources();
    if(gbuffer_pipeline_)vkDestroyPipeline(device_,gbuffer_pipeline_,nullptr);
    if(deferred_resolve_pipeline_)
        vkDestroyPipeline(device_,deferred_resolve_pipeline_,nullptr);
    if(deferred_resolve_layout_)
        vkDestroyPipelineLayout(device_,deferred_resolve_layout_,nullptr);
    if(gbuffer_pool_)vkDestroyDescriptorPool(device_,gbuffer_pool_,nullptr);
    if(gbuffer_layout_)vkDestroyDescriptorSetLayout(device_,gbuffer_layout_,nullptr);
    if(gbuffer_sampler_)vkDestroySampler(device_,gbuffer_sampler_,nullptr);
    if(deferred_tile_buffer_)
        vkDestroyBuffer(device_,deferred_tile_buffer_,nullptr);
    if(deferred_tile_memory_)
        vkFreeMemory(device_,deferred_tile_memory_,nullptr);
    deferred_tile_buffer_=VK_NULL_HANDLE;
    deferred_tile_memory_=VK_NULL_HANDLE;
    deferred_tile_bytes_=0;
    deferred_tile_cache_.invalidate();
    for(unsigned i=0;i<(gbuffer_v2_?4u:3u);++i) {
        if(gbuffer_views_[i])vkDestroyImageView(device_,gbuffer_views_[i],nullptr);
        if(gbuffer_images_[i])vkDestroyImage(device_,gbuffer_images_[i],nullptr);
        if(gbuffer_memories_[i])vkFreeMemory(device_,gbuffer_memories_[i],nullptr);
    }
    gbuffer_pipeline_=deferred_resolve_pipeline_=VK_NULL_HANDLE;
    deferred_resolve_layout_=VK_NULL_HANDLE;
    gbuffer_pool_=VK_NULL_HANDLE;gbuffer_layout_=VK_NULL_HANDLE;
    gbuffer_set_=VK_NULL_HANDLE;gbuffer_sampler_=VK_NULL_HANDLE;
    gbuffer_images_={};gbuffer_views_={};gbuffer_memories_={};
}
void Renderer::create_deferred_resources() {
    if(!deferred_enabled_)return;
    for(unsigned i=0;i<(gbuffer_v2_?4u:3u);++i) {
        VkFormatProperties features{};
        vkGetPhysicalDeviceFormatProperties(physical_,gbuffer_formats_[i],&features);
        constexpr auto needed=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|
                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if((features.optimalTilingFeatures&needed)!=needed)
            throw std::runtime_error("Vulkan MRT color format unsupported");
        VkImageCreateInfo desc{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        desc.imageType=VK_IMAGE_TYPE_2D;
        desc.extent={extent_.width,extent_.height,1};
        desc.mipLevels=desc.arrayLayers=1;
        desc.format=gbuffer_formats_[i];desc.samples=VK_SAMPLE_COUNT_1_BIT;
        desc.tiling=VK_IMAGE_TILING_OPTIMAL;
        desc.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        ensure(vkCreateImage(device_,&desc,nullptr,&gbuffer_images_[i]),
               "vkCreateImage(GBuffer)");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_,gbuffer_images_[i],&req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize=req.size;
        alloc.memoryTypeIndex=find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ensure(vkAllocateMemory(device_,&alloc,nullptr,&gbuffer_memories_[i]),
               "vkAllocateMemory(GBuffer)");
        ensure(vkBindImageMemory(device_,gbuffer_images_[i],
               gbuffer_memories_[i],0),"vkBindImageMemory(GBuffer)");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image=gbuffer_images_[i];view.format=gbuffer_formats_[i];
        view.viewType=VK_IMAGE_VIEW_TYPE_2D;
        view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount=1;view.subresourceRange.layerCount=1;
        ensure(vkCreateImageView(device_,&view,nullptr,&gbuffer_views_[i]),
               "vkCreateImageView(GBuffer)");
    }
    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.magFilter=s.minFilter=VK_FILTER_NEAREST;
    s.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    s.addressModeU=s.addressModeV=s.addressModeW=
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ensure(vkCreateSampler(device_,&s,nullptr,&gbuffer_sampler_),
           "vkCreateSampler(GBuffer)");
    create_ssao_resource();
    create_csm_resources();
    // GPU-visible tile grid: 32x32 pixel tiles, [count + 64 indices].
    deferred_tile_columns_=(extent_.width+31)/32;
    deferred_tile_rows_=(extent_.height+31)/32;
    deferred_tile_bytes_=VkDeviceSize(deferred_tile_columns_)*
        deferred_tile_rows_*northstarcpp::render::light_tile_stride*
        sizeof(std::uint32_t);
    VkBufferCreateInfo tile_desc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    tile_desc.size=deferred_tile_bytes_;
    tile_desc.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    ensure(vkCreateBuffer(device_,&tile_desc,nullptr,&deferred_tile_buffer_),
           "vkCreateBuffer(Vulkan tile lights)");
    VkMemoryRequirements tile_req{};
    vkGetBufferMemoryRequirements(device_,deferred_tile_buffer_,&tile_req);
    VkMemoryAllocateInfo tile_alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    tile_alloc.allocationSize=tile_req.size;
    tile_alloc.memoryTypeIndex=find_memory_type(tile_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    ensure(vkAllocateMemory(device_,&tile_alloc,nullptr,&deferred_tile_memory_),
           "vkAllocateMemory(Vulkan tile lights)");
    ensure(vkBindBufferMemory(device_,deferred_tile_buffer_,deferred_tile_memory_,0),
           "vkBindBufferMemory(Vulkan tile lights)");
    deferred_tile_cache_.invalidate();
    std::array<VkDescriptorSetLayoutBinding,10> bindings{};
    for(unsigned i=0;i<4;++i) {
        bindings[i].binding=i;bindings[i].descriptorCount=1;
        bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[4].binding=4;
    bindings[4].descriptorCount=1;
    bindings[4].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[4].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[5].binding=5;
    bindings[5].descriptorCount=1;
    bindings[5].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    if(gbuffer_v2_) {
        bindings[6].binding=6;
        bindings[6].descriptorCount=1;
        bindings[6].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[6].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    if(ssao_enabled_) {
        auto& b=bindings[gbuffer_v2_?7:6];
        b.binding=7;b.descriptorCount=1;
        b.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if(csm_enabled_) {
        const unsigned index=6u+(gbuffer_v2_?1u:0u)+(ssao_enabled_?1u:0u);
        bindings[index].binding=8;
        bindings[index].descriptorCount=1;
        bindings[index].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[index].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[index+1].binding=9;
        bindings[index+1].descriptorCount=1;
        bindings[index+1].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[index+1].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    layout.bindingCount=6u+(gbuffer_v2_?1u:0u)+(ssao_enabled_?1u:0u)+
                        (csm_enabled_?2u:0u);
    layout.pBindings=bindings.data();
    ensure(vkCreateDescriptorSetLayout(device_,&layout,nullptr,&gbuffer_layout_),
           "vkCreateDescriptorSetLayout(GBuffer)");
    std::array<VkDescriptorPoolSize,3> pool_sizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            4u+(gbuffer_v2_?1u:0u)+(ssao_enabled_?1u:0u)+(csm_enabled_?1u:0u)},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,csm_enabled_?2u:1u},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=1;pool.poolSizeCount=3;pool.pPoolSizes=pool_sizes.data();
    ensure(vkCreateDescriptorPool(device_,&pool,nullptr,&gbuffer_pool_),
           "vkCreateDescriptorPool(GBuffer)");
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool=gbuffer_pool_;ai.descriptorSetCount=1;
    ai.pSetLayouts=&gbuffer_layout_;
    ensure(vkAllocateDescriptorSets(device_,&ai,&gbuffer_set_),
           "vkAllocateDescriptorSets(GBuffer)");
    std::array<VkDescriptorImageInfo,7> images{};
    std::array<VkWriteDescriptorSet,10> writes{};
    for(unsigned i=0;i<4;++i) {
        images[i].sampler=gbuffer_sampler_;
        images[i].imageView=i==3?depth_view_:gbuffer_views_[i];
        images[i].imageLayout=i==3?
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet=gbuffer_set_;writes[i].dstBinding=i;
        writes[i].descriptorCount=1;
        writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo=&images[i];
    }
    VkDescriptorBufferInfo light_info{};
    light_info.buffer=lights_buffer_;
    light_info.offset=0;
    light_info.range=16+sizeof(NorthStarRenderLight)*
        NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS;
    writes[4].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet=gbuffer_set_;
    writes[4].dstBinding=4;
    writes[4].descriptorCount=1;
    writes[4].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[4].pBufferInfo=&light_info;
    VkDescriptorBufferInfo tiles{};
    tiles.buffer=deferred_tile_buffer_;tiles.offset=0;
    tiles.range=deferred_tile_bytes_;
    writes[5].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet=gbuffer_set_;
    writes[5].dstBinding=5;
    writes[5].descriptorCount=1;
    writes[5].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].pBufferInfo=&tiles;
    if(gbuffer_v2_) {
        images[4].sampler=gbuffer_sampler_;
        images[4].imageView=gbuffer_views_[3];
        images[4].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[6].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet=gbuffer_set_;
        writes[6].dstBinding=6;
        writes[6].descriptorCount=1;
        writes[6].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].pImageInfo=&images[4];
    }
    if(ssao_enabled_) {
        images[5].sampler=gbuffer_sampler_;
        images[5].imageView=ssao_view_;
        images[5].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        auto& w=writes[gbuffer_v2_?7:6];
        w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet=gbuffer_set_;w.dstBinding=7;
        w.descriptorCount=1;
        w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo=&images[5];
    }
    VkDescriptorBufferInfo shadow_uniform{csm_buffer_,0,sizeof(csm_uniform_)};
    if(csm_enabled_) {
        const unsigned index=6u+(gbuffer_v2_?1u:0u)+(ssao_enabled_?1u:0u);
        images[6].sampler=csm_sampler_;
        images[6].imageView=csm_array_view_;
        images[6].imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        auto& shadow_image_write=writes[index];
        shadow_image_write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadow_image_write.dstSet=gbuffer_set_;
        shadow_image_write.dstBinding=8;
        shadow_image_write.descriptorCount=1;
        shadow_image_write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadow_image_write.pImageInfo=&images[6];
        auto& shadow_buffer_write=writes[index+1];
        shadow_buffer_write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadow_buffer_write.dstSet=gbuffer_set_;
        shadow_buffer_write.dstBinding=9;
        shadow_buffer_write.descriptorCount=1;
        shadow_buffer_write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        shadow_buffer_write.pBufferInfo=&shadow_uniform;
    }
    vkUpdateDescriptorSets(device_,
        6u+(gbuffer_v2_?1u:0u)+(ssao_enabled_?1u:0u)+
            (csm_enabled_?2u:0u),
        writes.data(),0,nullptr);
}
void Renderer::create_deferred_pipelines() {
    if(!deferred_enabled_)return;
    VkShaderModule gfs=VK_NULL_HANDLE,gvs=VK_NULL_HANDLE,
                   vs=VK_NULL_HANDLE,fs=VK_NULL_HANDLE,
                   aofs=VK_NULL_HANDLE,vfs=VK_NULL_HANDLE;
    try {
        gvs=shader(device_,shaders::deferred_primitive_vertex,
                   sizeof(shaders::deferred_primitive_vertex));
        gfs=shader(device_,gbuffer_v2_?shaders::deferred_gbuffer_fragment_v2:
                            shaders::deferred_gbuffer_fragment,
                   gbuffer_v2_?sizeof(shaders::deferred_gbuffer_fragment_v2):
                   sizeof(shaders::deferred_gbuffer_fragment));
        vs=shader(device_,shaders::deferred_resolve_vertex,
                  sizeof(shaders::deferred_resolve_vertex));
        fs=shader(device_,gbuffer_v2_?(ssao_enabled_?shaders::deferred_resolve_fragment_v2_ssao:
                                     shaders::deferred_resolve_fragment_v2):
                          (ssao_enabled_?shaders::deferred_resolve_fragment_ssao:
                         shaders::deferred_resolve_fragment),
                  gbuffer_v2_?(ssao_enabled_?sizeof(shaders::deferred_resolve_fragment_v2_ssao):
                                      sizeof(shaders::deferred_resolve_fragment_v2)):
                   (ssao_enabled_?sizeof(shaders::deferred_resolve_fragment_ssao):
                                 sizeof(shaders::deferred_resolve_fragment)));
        if(csm_enabled_) {
            // Select the matching material layout and AO variant; the
            // ordinary resolve shader remains unchanged for CSM=off.
            vkDestroyShaderModule(device_,fs,nullptr);
            fs=VK_NULL_HANDLE;
            const auto* code=gbuffer_v2_?
                (ssao_enabled_?shaders::deferred_resolve_fragment_v2_ssao_csm:
                               shaders::deferred_resolve_fragment_v2_csm):
                (ssao_enabled_?shaders::deferred_resolve_fragment_ssao_csm:
                               shaders::deferred_resolve_fragment_csm);
            const auto bytes=gbuffer_v2_?
                (ssao_enabled_?sizeof(shaders::deferred_resolve_fragment_v2_ssao_csm):
                               sizeof(shaders::deferred_resolve_fragment_v2_csm)):
                (ssao_enabled_?sizeof(shaders::deferred_resolve_fragment_ssao_csm):
                               sizeof(shaders::deferred_resolve_fragment_csm));
            fs=shader(device_,code,bytes);
        }
        if(hdr_enabled_) {
            vkDestroyShaderModule(device_,fs,nullptr);
            fs=VK_NULL_HANDLE;
            const std::uint32_t* words=nullptr;
            std::size_t word_bytes=0;
            if(gbuffer_v2_==false && ssao_enabled_==false && csm_enabled_==false) {
                words=shaders::deferred_resolve_fragment_hdr;
                word_bytes=sizeof(shaders::deferred_resolve_fragment_hdr);
            }
            else if(gbuffer_v2_==true && ssao_enabled_==false && csm_enabled_==false) {
                words=shaders::deferred_resolve_fragment_v2_hdr;
                word_bytes=sizeof(shaders::deferred_resolve_fragment_v2_hdr);
            }
            else if(gbuffer_v2_==false && ssao_enabled_==true && csm_enabled_==false) {
                words=shaders::deferred_resolve_fragment_ssao_hdr;
                word_bytes=sizeof(shaders::deferred_resolve_fragment_ssao_hdr);
            }
            else if(gbuffer_v2_==true && ssao_enabled_==true && csm_enabled_==false) {
                words=shaders::deferred_resolve_fragment_v2_ssao_hdr;
                word_bytes=sizeof(shaders::deferred_resolve_fragment_v2_ssao_hdr);
            }
            else if(gbuffer_v2_==false && ssao_enabled_==false && csm_enabled_==true) {
                words=shaders::deferred_resolve_fragment_csm_hdr;
                word_bytes=sizeof(shaders::deferred_resolve_fragment_csm_hdr);
            }
            else if(gbuffer_v2_==true && ssao_enabled_==false && csm_enabled_==true) {
                words=shaders::deferred_resolve_fragment_v2_csm_hdr;
                word_bytes=sizeof(shaders::deferred_resolve_fragment_v2_csm_hdr);
            }
            else if(gbuffer_v2_==false && ssao_enabled_==true && csm_enabled_==true) {
                words=shaders::deferred_resolve_fragment_ssao_csm_hdr;
                word_bytes=sizeof(shaders::deferred_resolve_fragment_ssao_csm_hdr);
            }
            else if(gbuffer_v2_==true && ssao_enabled_==true && csm_enabled_==true) {
                words=shaders::deferred_resolve_fragment_v2_ssao_csm_hdr;
                word_bytes=sizeof(shaders::deferred_resolve_fragment_v2_ssao_csm_hdr);
            }
            fs=shader(device_,words,word_bytes);
        }
        if(ssao_enabled_)
            aofs=shader(device_,shaders::deferred_ssao_fragment,
                        sizeof(shaders::deferred_ssao_fragment));
        if(velocity_enabled_)
            vfs=shader(device_,shaders::deferred_velocity_fragment,
                       sizeof(shaders::deferred_velocity_fragment));
        std::array<VkPipelineShaderStageCreateInfo,2> stages{};
        auto assign_stages=[&](VkShaderModule vertex,VkShaderModule fragment) {
            for(unsigned i=0;i<2;++i) {
                stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
                stages[i].module=i?fragment:vertex;
                stages[i].pName="main";
            }
        };
        assign_stages(gvs,gfs);
        VkVertexInputBindingDescription binding{
            0,sizeof(ColoredVertex),VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription,3> attrs{{
            {0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0},
            {1,0,VK_FORMAT_R32G32B32A32_SFLOAT,16},
            {2,0,VK_FORMAT_R32G32B32A32_SFLOAT,32}}};
        VkPipelineVertexInputStateCreateInfo vi{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount=1;vi.pVertexBindingDescriptions=&binding;
        vi.vertexAttributeDescriptionCount=3;vi.pVertexAttributeDescriptions=attrs.data();
        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount=vp.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode=VK_POLYGON_MODE_FILL;
        raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth=1.f;
        VkPipelineMultisampleStateCreateInfo ms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable=VK_TRUE;depth.depthWriteEnable=VK_TRUE;
        depth.depthCompareOp=VK_COMPARE_OP_LESS;
        std::array<VkPipelineColorBlendAttachmentState,4> writes{};
        for(auto& a:writes)a.colorWriteMask=0xf;
        VkPipelineColorBlendStateCreateInfo cb{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount=gbuffer_v2_?4u:3u;cb.pAttachments=writes.data();
        VkDynamicState dynamics[2]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount=2;dyn.pDynamicStates=dynamics;
        VkPipelineRenderingCreateInfo rendering{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount=gbuffer_v2_?4u:3u;
        rendering.pColorAttachmentFormats=gbuffer_formats_.data();
        rendering.depthAttachmentFormat=depth_format_;
        VkGraphicsPipelineCreateInfo ci{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.pNext=&rendering;ci.stageCount=2;ci.pStages=stages.data();
        ci.pVertexInputState=&vi;ci.pInputAssemblyState=&ia;
        ci.pViewportState=&vp;ci.pRasterizationState=&raster;
        ci.pMultisampleState=&ms;ci.pDepthStencilState=&depth;
        ci.pColorBlendState=&cb;ci.pDynamicState=&dyn;
        ci.layout=pipeline_layout_;
        ensure(create_cached_graphics_pipelines(1,&ci,&gbuffer_pipeline_),"vkCreateGraphicsPipelines(GBuffer)");
        // Fullscreen triangle: no vertex input or depth attachment.
        assign_stages(vs,fs);
        vi.vertexBindingDescriptionCount=vi.vertexAttributeDescriptionCount=0;
        vi.pVertexBindingDescriptions=nullptr;vi.pVertexAttributeDescriptions=nullptr;
        depth.depthTestEnable=depth.depthWriteEnable=VK_FALSE;
        cb.attachmentCount=1;
        rendering.colorAttachmentCount=1;
        VkFormat resolve_format=hdr_enabled_?hdr_format_:format_;
        rendering.pColorAttachmentFormats=&resolve_format;
        rendering.depthAttachmentFormat=VK_FORMAT_UNDEFINED;
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,128};
        VkPipelineLayoutCreateInfo pli{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.setLayoutCount=1;pli.pSetLayouts=&gbuffer_layout_;
        pli.pushConstantRangeCount=1;pli.pPushConstantRanges=&push;
        ensure(vkCreatePipelineLayout(device_,&pli,nullptr,
               &deferred_resolve_layout_),"vkCreatePipelineLayout(Resolve)");
        ci.layout=deferred_resolve_layout_;
        ensure(create_cached_graphics_pipelines(1,&ci,&deferred_resolve_pipeline_),
               "vkCreateGraphicsPipelines(Resolve)");
        if(velocity_enabled_) {
            assign_stages(vs,vfs);
            VkFormat motion_format=VK_FORMAT_R16G16_SFLOAT;
            rendering.pColorAttachmentFormats=&motion_format;
            ensure(create_cached_graphics_pipelines(
                1,&ci,&velocity_pipeline_),"vkCreateGraphicsPipelines(Velocity)");
        }
        if(ssao_enabled_) {
            assign_stages(vs,aofs);
            VkFormat ao_format=VK_FORMAT_R16G16_SFLOAT;
            rendering.pColorAttachmentFormats=&ao_format;
            ensure(create_cached_graphics_pipelines(1,&ci,&ssao_pipeline_),
                   "vkCreateGraphicsPipelines(SSAO)");
        }
    }catch(...) {
        if(gfs)vkDestroyShaderModule(device_,gfs,nullptr);
        if(gvs)vkDestroyShaderModule(device_,gvs,nullptr);
        if(vs)vkDestroyShaderModule(device_,vs,nullptr);
        if(fs)vkDestroyShaderModule(device_,fs,nullptr);
        if(aofs)vkDestroyShaderModule(device_,aofs,nullptr);
        if(vfs)vkDestroyShaderModule(device_,vfs,nullptr);
        throw;
    }
    vkDestroyShaderModule(device_,gfs,nullptr);
    vkDestroyShaderModule(device_,gvs,nullptr);
    vkDestroyShaderModule(device_,vs,nullptr);
    vkDestroyShaderModule(device_,fs,nullptr);
    if(aofs)vkDestroyShaderModule(device_,aofs,nullptr);
    if(vfs)vkDestroyShaderModule(device_,vfs,nullptr);
    if(csm_enabled_)create_csm_pipelines();
}
void Renderer::update_deferred_tiles(NorthStarRenderFrame const& frame) {
    if(!deferred_enabled_)return;
    const float fov=frame.camera_fov_y_degrees>1.f?
        frame.camera_fov_y_degrees:70.f;
    const float scale=1.f/std::tan(
        fov*3.14159265358979323846f/360.f);
    const float aspect=float(extent_.width)/float(extent_.height);
    const float cy=std::cos(frame.camera_yaw),sy=std::sin(frame.camera_yaw);
    const float cp=std::cos(frame.camera_pitch),sp=std::sin(frame.camera_pitch);
    // The CPU tile-frustum helper expects row-major inverse-projection
    // rays. Construct their world basis analytically from the same camera
    // equations that generate the actual Vulkan clip-space vertices.
    std::array<float,16> const rays{
         cy*aspect/scale,0,-sy*aspect/scale,0,
        -sy*sp/scale,cp/scale,-cy*sp/scale,0,
         sy*cp,sp,cy*cp,0,
         frame.camera_x,frame.camera_y,frame.camera_z,1
    };
    std::array<float,3> const eye{
        frame.camera_x,frame.camera_y,frame.camera_z};
    const bool changed=deferred_tile_cache_.update(
        extent_.width,extent_.height,rays,eye,
        dynamic_lights_.empty()?nullptr:dynamic_lights_.data(),
        static_cast<std::uint32_t>(dynamic_lights_.size()));
    if(!changed)return;
    const auto& indices=deferred_tile_cache_.grid().indices;
    const auto bytes=VkDeviceSize(indices.size())*sizeof(std::uint32_t);
    if(bytes!=deferred_tile_bytes_)
        throw std::logic_error("Vulkan tiled light GPU buffer size mismatch");
    void* mapped=nullptr;
    ensure(vkMapMemory(device_,deferred_tile_memory_,0,bytes,0,&mapped),
           "vkMapMemory(Vulkan tiled light indices)");
    std::memcpy(mapped,indices.data(),std::size_t(bytes));
    vkUnmapMemory(device_,deferred_tile_memory_);
}
void Renderer::record_gbuffer(VkCommandBuffer cmd,
                              NorthStarRenderFrame const&) {
    std::array<VkRenderingAttachmentInfo,4> targets{};
    for(unsigned i=0;i<(gbuffer_v2_?4u:3u);++i) {
        auto& a=targets[i];
        a.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        a.imageView=gbuffer_views_[i];
        a.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        if(i==3)a.clearValue.color.float32[0]=1.f; // legacy AO neutral
    }
    VkRenderingAttachmentInfo depth{
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView=depth_view_;
    depth.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil={1.f,0};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent=extent_;
    info.layerCount=1;info.colorAttachmentCount=gbuffer_v2_?4u:3u;
    info.pColorAttachments=targets.data();info.pDepthAttachment=&depth;
    vkCmdBeginRendering(cmd,&info);
    if(render_vertices_) {
        VkViewport vp{0,0,float(extent_.width),float(extent_.height),0,1};
        VkRect2D scissor{};scissor.extent=extent_;
        vkCmdSetViewport(cmd,0,1,&vp);vkCmdSetScissor(cmd,0,1,&scissor);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,gbuffer_pipeline_);
        VkDeviceSize const offset=0;
        vkCmdBindVertexBuffers(cmd,0,1,&vertices_buffer_,&offset);
        vkCmdDraw(cmd,render_vertices_,1,0,0);
    }
    // All authored opaque, masked and GPU-skinned draws now share MRT depth.
    VkViewport vp{0,0,float(extent_.width),float(extent_.height),0,1};
    VkRect2D scissor{};scissor.extent=extent_;
    vkCmdSetViewport(cmd,0,1,&vp);vkCmdSetScissor(cmd,0,1,&scissor);
    VkDeviceSize const offset=0;
    if(!skin_draws_.empty()) {
        vkCmdBindVertexBuffers(cmd,0,1,&skin_output_.handle,&offset);
        for(auto const& draw:skin_draws_) {
            if(!draw.gbuffer_pipeline)continue;
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                              draw.gbuffer_pipeline);
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                material_pipeline_layout_,0,1,&draw.textures,0,nullptr);
            vkCmdDraw(cmd,draw.count,1,draw.first,0);
        }
    }
    if(!material_draws_.empty()) {
        vkCmdBindVertexBuffers(cmd,0,1,&material_vertices_buffer_,&offset);
        for(auto const& draw:material_draws_) {
            if(draw.blend_mode==2||!draw.gbuffer_pipeline)continue;
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                              draw.gbuffer_pipeline);
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                material_pipeline_layout_,0,1,&draw.textures,0,nullptr);
            vkCmdDraw(cmd,draw.count,1,draw.first,0);
        }
    }
    vkCmdEndRendering(cmd);
}
void Renderer::record_deferred_resolve(VkCommandBuffer cmd,std::uint32_t image) {
    VkRenderingAttachmentInfo color{
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView=hdr_enabled_?hdr_scene_view_:image_views_.at(image);
    color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent=extent_;info.layerCount=1;
    info.colorAttachmentCount=1;info.pColorAttachments=&color;
    vkCmdBeginRendering(cmd,&info);
    VkViewport vp{0,0,float(extent_.width),float(extent_.height),0,1};
    VkRect2D scissor{};scissor.extent=extent_;
    vkCmdSetViewport(cmd,0,1,&vp);vkCmdSetScissor(cmd,0,1,&scissor);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,deferred_resolve_pipeline_);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
        deferred_resolve_layout_,0,1,&gbuffer_set_,0,nullptr);
    struct Push {
        float sky[4],mode[4],camera[4],projection[4],viewport[4],ao[4],film[4],film_extra[4];
    } push{};
    static_assert(sizeof(Push)==128);
    for(unsigned i=0;i<4;++i)push.sky[i]=sky_clear_[i];
    // The fourth sky push component was unused by the resolve shader.
    // Reuse it for the optional fog density without exceeding the
    // Vulkan guaranteed minimum 128-byte push constant limit.
    push.sky[3]=height_fog_density_;
    push.camera[0]=pick_camera_.camera_x;
    push.camera[1]=pick_camera_.camera_y;
    push.camera[2]=pick_camera_.camera_z;
    push.camera[3]=pick_camera_.camera_yaw;
    float near_clip=pick_camera_.camera_near_clip>0.f?
        pick_camera_.camera_near_clip:.1f;
    float far_clip=pick_camera_.camera_far_clip>near_clip?
        pick_camera_.camera_far_clip:250.f;
    float fov=pick_camera_.camera_fov_y_degrees>1.f?
        pick_camera_.camera_fov_y_degrees:70.f;
    push.projection[0]=pick_camera_.camera_pitch;
    push.projection[1]=near_clip;
    push.projection[2]=far_clip;
    push.projection[3]=1.f/std::tan(fov*3.14159265358979323846f/360.f);
    push.viewport[0]=float(extent_.width);
    push.viewport[1]=float(extent_.height);
    push.viewport[2]=1.f/float(extent_.width);
    push.viewport[3]=1.f/float(extent_.height);
    push.film[0]=film_look_.film_grain;
    push.film[1]=film_look_.vignette;
    push.film[2]=0.f; // Full-frame chromatic aberration not yet supported in Vulkan
    push.film[3]=film_look_.saturation;
    push.film_extra[0]=film_look_.contrast;
    push.film_extra[1]=film_look_.exposure_ev;
    // Reuse the reserved push-constant slot; no renderer/provider ABI change.
    push.film_extra[2]=contact_shadows_enabled_?1.f:0.f;
    push.film_extra[3]=float((film_frame_++)%4096u);
    push.ao[0]=ssao_enabled_?1.f:0.f;
    push.ao[1]=2.f; // world-space radius; modest default for editor QA
    push.ao[2]=2.f; // ambient-only intensity
    push.ao[3]=.035f; // surface-angle bias
    push.mode[1]=float(deferred_tile_columns_);
    push.mode[2]=32.f;
    push.mode[3]=float(deferred_tile_rows_);
    auto* mode=std::getenv("NORTHSTARCPP_GBUFFER_DEBUG");
    if(!mode)mode=std::getenv("NORTHSTARCPP_VK_GBUFFER_DEBUG");
    if(mode) {
        if(!std::strcmp(mode,"albedo"))push.mode[0]=1;
        else if(!std::strcmp(mode,"normal"))push.mode[0]=2;
        else if(!std::strcmp(mode,"roughness"))push.mode[0]=3;
        else if(!std::strcmp(mode,"metallic"))push.mode[0]=4;
        else if(!std::strcmp(mode,"emissive"))push.mode[0]=5;
        else if(!std::strcmp(mode,"depth"))push.mode[0]=6;
        else if(!std::strcmp(mode,"ao"))push.mode[0]=7;
        else if(!std::strcmp(mode,"coverage"))push.mode[0]=8;
        else if(!std::strcmp(mode,"material_ao"))push.mode[0]=9;
        else if(!std::strcmp(mode,"clearcoat"))push.mode[0]=10;
        else if(!std::strcmp(mode,"sheen"))push.mode[0]=11;
        else if(!std::strcmp(mode,"contact_shadow"))push.mode[0]=12;
        else if(!std::strcmp(mode,"sun_shadow"))push.mode[0]=13;
    }
    vkCmdPushConstants(cmd,deferred_resolve_layout_,
        VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
    vkCmdDraw(cmd,3,1,0,0);
    vkCmdEndRendering(cmd);
}
void Renderer::record_deferred_forward(VkCommandBuffer cmd,std::uint32_t image) {
    // Material/skin forward composition is deliberately separate until their
    // shader variants emit the same GBuffer packing as the opaque primitives.
    // The existing color/depth PSOs retain correct depth testing here.
    bool any_translucent=false;
    for(auto const& draw:material_draws_)
        any_translucent|=draw.blend_mode==2;
    if(!any_translucent)return;
    VkRenderingAttachmentInfo color{
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView=hdr_enabled_?hdr_scene_view_:image_views_.at(image);
    color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth{
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView=depth_view_;
    depth.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent=extent_;info.layerCount=1;
    info.colorAttachmentCount=1;info.pColorAttachments=&color;
    info.pDepthAttachment=&depth;
    vkCmdBeginRendering(cmd,&info);
    VkViewport vp{0,0,float(extent_.width),float(extent_.height),0,1};
    VkRect2D scissor{};scissor.extent=extent_;
    vkCmdSetViewport(cmd,0,1,&vp);vkCmdSetScissor(cmd,0,1,&scissor);
    VkDeviceSize const offset=0;
    if(!material_draws_.empty()) {
        vkCmdBindVertexBuffers(cmd,0,1,&material_vertices_buffer_,&offset);
        for(auto const& batch:material_draws_) {
            if(batch.blend_mode!=2)continue;
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,batch.pipeline);
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                material_pipeline_layout_,0,1,&batch.textures,0,nullptr);
            vkCmdDraw(cmd,batch.count,1,batch.first,0);
        }
    }
    vkCmdEndRendering(cmd);
}


} // namespace northstarcpp::render::vulkan
