// Vulkan 1.3 depth-only 3-cascade rendering: native geometry + skinned mesh.
#include "renderer_private.hpp"
#include "deferred_spirv.hpp"
#include <cstddef>
#include <stdexcept>
#include <string>
namespace northstarcpp::render::vulkan {
namespace {
void shadow_check(VkResult code,const char* what) {
    if(code!=VK_SUCCESS)throw std::runtime_error(
        std::string(what)+" VkResult="+std::to_string(int(code)));
}
}
void Renderer::create_csm_pipelines() {
    if(!csm_enabled_)return;
    VkShaderModule vertex=VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize=sizeof(shaders::deferred_shadow_vertex);
    shader.pCode=shaders::deferred_shadow_vertex;
    shadow_check(vkCreateShaderModule(device_,&shader,nullptr,&vertex),
                 "vkCreateShaderModule(CSM)");
    try {
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(unsigned)};
        VkPipelineLayoutCreateInfo layout{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount=1;layout.pSetLayouts=&csm_descriptor_layout_;
        layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
        shadow_check(vkCreatePipelineLayout(device_,&layout,nullptr,
                     &csm_pipeline_layout_),"vkCreatePipelineLayout(CSM)");
        VkPipelineShaderStageCreateInfo stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage=VK_SHADER_STAGE_VERTEX_BIT;
        stage.module=vertex;stage.pName="main";
        VkVertexInputBindingDescription binding{
            0,sizeof(ColoredVertex),VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attribute{
            0,0,VK_FORMAT_R32G32B32_SFLOAT,
            offsetof(ColoredVertex,world)};
        VkPipelineVertexInputStateCreateInfo input{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount=1;
        input.pVertexBindingDescriptions=&binding;
        input.vertexAttributeDescriptionCount=1;
        input.pVertexAttributeDescriptions=&attribute;
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount=viewport.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode=VK_POLYGON_MODE_FILL;
        raster.cullMode=VK_CULL_MODE_NONE;
        raster.lineWidth=1.f;raster.depthBiasEnable=VK_TRUE;
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable=depth.depthWriteEnable=VK_TRUE;
        depth.depthCompareOp=VK_COMPARE_OP_LESS;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        VkDynamicState dynamics[]{
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount=3;dynamic.pDynamicStates=dynamics;
        VkPipelineRenderingCreateInfo rendering{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount=0;
        rendering.depthAttachmentFormat=VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo pipeline{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.pNext=&rendering;
        pipeline.stageCount=1;pipeline.pStages=&stage;
        pipeline.pVertexInputState=&input;
        pipeline.pInputAssemblyState=&assembly;
        pipeline.pViewportState=&viewport;
        pipeline.pRasterizationState=&raster;
        pipeline.pMultisampleState=&multisample;
        pipeline.pDepthStencilState=&depth;
        pipeline.pColorBlendState=&blend;
        pipeline.pDynamicState=&dynamic;
        pipeline.layout=csm_pipeline_layout_;
        shadow_check(create_cached_graphics_pipelines(1,&pipeline,&csm_primitive_pipeline_),
                     "vkCreateGraphicsPipelines(CSM primitive)");
        binding.stride=sizeof(MaterialClipVertex);
        attribute.offset=offsetof(MaterialClipVertex,world);
        shadow_check(create_cached_graphics_pipelines(1,&pipeline,&csm_material_pipeline_),
                     "vkCreateGraphicsPipelines(CSM material)");
    } catch(...) {
        vkDestroyShaderModule(device_,vertex,nullptr);
        throw;
    }
    vkDestroyShaderModule(device_,vertex,nullptr);
}
void Renderer::record_csm_pass(VkCommandBuffer cmd) {
    if(!csm_enabled_)return;
    update_csm_matrices();
    const VkDeviceSize offset=0;
    VkViewport viewport{0,0,float(csm_resolution_),
                        float(csm_resolution_),0,1};
    VkRect2D scissor{};scissor.extent={csm_resolution_,csm_resolution_};
    for(unsigned cascade=0;cascade<csm_cascades_;++cascade) {
        VkRenderingAttachmentInfo depth{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depth.imageView=csm_layer_views_[cascade];
        depth.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil={1.f,0u};
        VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
        info.renderArea.extent={csm_resolution_,csm_resolution_};
        info.layerCount=1;info.pDepthAttachment=&depth;
        vkCmdBeginRendering(cmd,&info);
        vkCmdSetViewport(cmd,0,1,&viewport);
        vkCmdSetScissor(cmd,0,1,&scissor);
        vkCmdSetDepthBias(cmd,1.25f,0.f,1.75f);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
            csm_pipeline_layout_,0,1,&csm_descriptor_set_,0,nullptr);
        vkCmdPushConstants(cmd,csm_pipeline_layout_,VK_SHADER_STAGE_VERTEX_BIT,
                           0,sizeof(cascade),&cascade);
        if(render_vertices_) {
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                              csm_primitive_pipeline_);
            vkCmdBindVertexBuffers(cmd,0,1,&vertices_buffer_,&offset);
            vkCmdDraw(cmd,render_vertices_,1,0,0);
        }
        if(!skin_draws_.empty()) {
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                              csm_material_pipeline_);
            vkCmdBindVertexBuffers(cmd,0,1,&skin_output_.handle,&offset);
            for(auto const& draw:skin_draws_)
                if(draw.gbuffer_pipeline && draw.count)
                    vkCmdDraw(cmd,draw.count,1,draw.first,0);
        }
        if(!material_draws_.empty()) {
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                              csm_material_pipeline_);
            vkCmdBindVertexBuffers(cmd,0,1,&material_vertices_buffer_,&offset);
            for(auto const& draw:material_draws_) {
                // Alpha-masked casters need a separate cutout fragment shader.
                // Skip rather than creating an incorrect solid shadow.
                if(draw.blend_mode==0 && draw.gbuffer_pipeline && draw.count)
                    vkCmdDraw(cmd,draw.count,1,draw.first,0);
            }
        }
        vkCmdEndRendering(cmd);
    }
}
} // namespace northstarcpp::render::vulkan
