// HDR bloom pyramid & final presentation pass. UI never enters HDR buffers.
#include "renderer_private.hpp"
#include "deferred_spirv.hpp"
#include <stdexcept>
#include <string>
namespace northstarcpp::render::vulkan {
namespace {
void ensure_fx(VkResult r,const char* op){
    if(r!=VK_SUCCESS)throw std::runtime_error(std::string(op)+
        " VkResult="+std::to_string(int(r)));
}
VkShaderModule compile_embedded(VkDevice device,
                               const std::uint32_t* words,std::size_t bytes) {
    VkShaderModuleCreateInfo desc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    desc.codeSize=bytes;desc.pCode=words;
    VkShaderModule module=VK_NULL_HANDLE;
    ensure_fx(vkCreateShaderModule(device,&desc,nullptr,&module),
              "vkCreateShaderModule(PostFX)");
    return module;
}
}
void Renderer::create_hdr_pipelines(){
    if(!hdr_enabled_)return;
    VkShaderModule vertex=VK_NULL_HANDLE,bloom=VK_NULL_HANDLE,
                   composite=VK_NULL_HANDLE,luminance=VK_NULL_HANDLE;
    try {
        vertex=compile_embedded(device_,shaders::deferred_resolve_vertex,
                               sizeof(shaders::deferred_resolve_vertex));
        bloom=compile_embedded(device_,shaders::deferred_bloom_fragment,
                              sizeof(shaders::deferred_bloom_fragment));
        composite=compile_embedded(device_,shaders::deferred_composite_fragment,
                                  sizeof(shaders::deferred_composite_fragment));
        if(auto_exposure_enabled_)
            luminance=compile_embedded(device_,shaders::deferred_luminance_fragment,
                sizeof(shaders::deferred_luminance_fragment));
        VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT,0,32};
        VkPipelineLayoutCreateInfo layout{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount=1;layout.pSetLayouts=&bloom_set_layout_;
        layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&range;
        ensure_fx(vkCreatePipelineLayout(device_,&layout,nullptr,
                  &bloom_pipeline_layout_),
                  "vkCreatePipelineLayout(Bloom)");
        range.size=64;
        layout.pSetLayouts=&composite_set_layout_;
        ensure_fx(vkCreatePipelineLayout(device_,&layout,nullptr,
                  &composite_pipeline_layout_),
                  "vkCreatePipelineLayout(Composite)");
        VkPipelineShaderStageCreateInfo stages[2]{};
        for(unsigned i=0;i<2;++i) {
            stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module=i?bloom:vertex;
            stages[i].pName="main";
        }
        VkPipelineVertexInputStateCreateInfo input{
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
        raster.lineWidth=1.f;
        VkPipelineMultisampleStateCreateInfo ms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable=depth.depthWriteEnable=VK_FALSE;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask=0xF;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount=1;blend.pAttachments=&attachment;
        VkDynamicState dynamic_states[]{
            VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamic_states;
        VkPipelineRenderingCreateInfo rendering{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount=1;
        VkFormat bloom_format=hdr_format_;
        rendering.pColorAttachmentFormats=&bloom_format;
        VkGraphicsPipelineCreateInfo pipeline{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.pNext=&rendering;
        pipeline.stageCount=2;pipeline.pStages=stages;
        pipeline.pVertexInputState=&input;
        pipeline.pInputAssemblyState=&assembly;
        pipeline.pViewportState=&viewport;
        pipeline.pRasterizationState=&raster;
        pipeline.pMultisampleState=&ms;
        pipeline.pDepthStencilState=&depth;
        pipeline.pColorBlendState=&blend;
        pipeline.pDynamicState=&dynamic;
        pipeline.layout=bloom_pipeline_layout_;
        ensure_fx(create_cached_graphics_pipelines(1,&pipeline,&bloom_pipeline_),
                  "vkCreateGraphicsPipelines(Bloom)");
        if(auto_exposure_enabled_) {
            stages[1].module=luminance;
            VkFormat meter_format=VK_FORMAT_R16G16_SFLOAT;
            rendering.pColorAttachmentFormats=&meter_format;
            ensure_fx(create_cached_graphics_pipelines(1,&pipeline,
                      &luminance_pipeline_),"vkCreateGraphicsPipelines(Luminance)");
        }
        stages[1].module=composite;
        rendering.pColorAttachmentFormats=&format_;
        pipeline.layout=composite_pipeline_layout_;
        ensure_fx(create_cached_graphics_pipelines(1,&pipeline,
                  &composite_pipeline_),"vkCreateGraphicsPipelines(Composite)");
    }catch(...){
        if(vertex)vkDestroyShaderModule(device_,vertex,nullptr);
        if(bloom)vkDestroyShaderModule(device_,bloom,nullptr);
        if(composite)vkDestroyShaderModule(device_,composite,nullptr);
        if(luminance)vkDestroyShaderModule(device_,luminance,nullptr);
        throw;
    }
    vkDestroyShaderModule(device_,vertex,nullptr);
    vkDestroyShaderModule(device_,bloom,nullptr);
    vkDestroyShaderModule(device_,composite,nullptr);
    if(luminance)vkDestroyShaderModule(device_,luminance,nullptr);
}
void Renderer::record_hdr_bloom(VkCommandBuffer cmd,unsigned level) {
    if(!hdr_enabled_||level>=bloom_levels_)return;
    VkRenderingAttachmentInfo target{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    target.imageView=bloom_views_[level];
    target.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    target.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    target.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent=bloom_extents_[level];
    rendering.layerCount=1;rendering.colorAttachmentCount=1;
    rendering.pColorAttachments=&target;
    vkCmdBeginRendering(cmd,&rendering);
    const auto e=bloom_extents_[level];
    VkViewport viewport{0,0,float(e.width),float(e.height),0,1};
    VkRect2D scissor{};scissor.extent=e;
    vkCmdSetViewport(cmd,0,1,&viewport);
    vkCmdSetScissor(cmd,0,1,&scissor);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,bloom_pipeline_);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
        bloom_pipeline_layout_,0,1,&bloom_descriptor_sets_[level],0,nullptr);
    struct BloomPush {
        float target[4],bloom[4];
    } push{};
    static_assert(sizeof(push)==32);
    push.target[0]=float(e.width);push.target[1]=float(e.height);
    push.target[2]=1.f/float(e.width);push.target[3]=1.f/float(e.height);
    push.bloom[0]=bloom_threshold_;
    push.bloom[1]=bloom_soft_knee_;
    push.bloom[2]=float(bloom_quality_);
    push.bloom[3]=float(level);
    vkCmdPushConstants(cmd,bloom_pipeline_layout_,VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,sizeof(push),&push);
    vkCmdDraw(cmd,3,1,0,0);
    vkCmdEndRendering(cmd);
}
void Renderer::record_hdr_compositor(VkCommandBuffer cmd,std::uint32_t image) {
    if(!hdr_enabled_)return;
    VkRenderingAttachmentInfo target{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    target.imageView=image_views_.at(image);
    target.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    target.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    target.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent=extent_;
    rendering.layerCount=1;rendering.colorAttachmentCount=1;
    rendering.pColorAttachments=&target;
    vkCmdBeginRendering(cmd,&rendering);
    VkViewport viewport{0,0,float(extent_.width),float(extent_.height),0,1};
    VkRect2D scissor{};scissor.extent=extent_;
    vkCmdSetViewport(cmd,0,1,&viewport);
    vkCmdSetScissor(cmd,0,1,&scissor);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,composite_pipeline_);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
        composite_pipeline_layout_,0,1,&composite_descriptor_set_,0,nullptr);
    struct CompositePush {
        float viewport[4],look[4],film[4],flags[4];
    } push{};
    static_assert(sizeof(push)==64);
    push.viewport[0]=float(extent_.width);
    push.viewport[1]=float(extent_.height);
    push.viewport[2]=1.f/float(extent_.width);
    push.viewport[3]=1.f/float(extent_.height);
    push.look[0]=film_look_.exposure_ev+
                 (auto_exposure_enabled_?adapted_exposure_ev_:0.f);
    push.look[1]=film_look_.contrast;
    push.look[2]=film_look_.saturation;
    push.look[3]=active_bloom_levels_?bloom_strength_:0.f;
    push.film[0]=film_look_.vignette;
    push.film[1]=film_look_.film_grain;
    push.film[2]=film_look_.chromatic_pixels;
    push.film[3]=float(film_frame_%4096u);
    push.flags[1]=float(active_bloom_levels_);
    push.flags[3]=velocity_debug_?1.f:0.f;
    // On frame 0 the CPU has no completed-frame history yet; the compositor
    // derives its first exposure from the meter rendered earlier this frame.
    push.flags[2]=auto_exposure_enabled_ &&
        exposure_last_frame_.time_since_epoch().count()==0 ? 1.f:0.f;
    push.flags[0]=(format_==VK_FORMAT_B8G8R8A8_SRGB ||
                    format_==VK_FORMAT_R8G8B8A8_SRGB)?1.f:0.f;
    vkCmdPushConstants(cmd,composite_pipeline_layout_,
                       VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
    vkCmdDraw(cmd,3,1,0,0);
    vkCmdEndRendering(cmd);
}
}
