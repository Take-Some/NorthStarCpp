// Opt-in camera/depth GPU motion vector pass. This is the initial camera
// component of the future object/skinned motion-vector composite.
#include "renderer_private.hpp"
#include <array>
#include <cmath>
#include <stdexcept>
namespace northstarcpp::render::vulkan {
void Renderer::record_hdr_velocity(VkCommandBuffer cmd) {
    if(!velocity_enabled_ || !velocity_pipeline_ || !velocity_view_)
        throw std::logic_error("Vulkan GPU velocity pipeline not initialized");
    VkRenderingAttachmentInfo target{
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    target.imageView=velocity_view_;
    target.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    target.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    target.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    target.clearValue.color={{0.f,0.f,0.f,0.f}};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent=extent_;
    info.layerCount=1;
    info.colorAttachmentCount=1;
    info.pColorAttachments=&target;
    vkCmdBeginRendering(cmd,&info);
    VkViewport viewport{0,0,float(extent_.width),float(extent_.height),0,1};
    VkRect2D scissor{};scissor.extent=extent_;
    vkCmdSetViewport(cmd,0,1,&viewport);
    vkCmdSetScissor(cmd,0,1,&scissor);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,velocity_pipeline_);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
        deferred_resolve_layout_,0,1,&gbuffer_set_,0,nullptr);
    struct MotionPush {
        float current_camera[4],current_proj[4];
        float previous_camera[4],previous_proj[4];
        float viewport[4],options[4];
    } push{};
    static_assert(sizeof(push)==96);
    const auto& c=temporal_frame_.current;
    const auto& p=temporal_frame_.previous;
    constexpr float radians=3.14159265358979323846f/180.f;
    auto fill=[&](northstarcpp::render::TemporalCamera const& camera,
                  float (&position)[4],float (&projection)[4]) {
        position[0]=camera.position[0];
        position[1]=camera.position[1];
        position[2]=camera.position[2];
        position[3]=camera.yaw;
        projection[0]=camera.pitch;
        projection[1]=camera.near_clip;
        projection[2]=camera.far_clip;
        projection[3]=1.f/std::tan(camera.fov_y_degrees*radians*.5f);
    };
    fill(c,push.current_camera,push.current_proj);
    fill(p,push.previous_camera,push.previous_proj);
    push.viewport[0]=float(extent_.width);
    push.viewport[1]=float(extent_.height);
    push.viewport[2]=1.f/float(extent_.width);
    push.viewport[3]=1.f/float(extent_.height);
    push.options[0]=temporal_frame_.history_valid?1.f:0.f;
    vkCmdPushConstants(cmd,deferred_resolve_layout_,
        VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
    vkCmdDraw(cmd,3,1,0,0);
    vkCmdEndRendering(cmd);
}
}
