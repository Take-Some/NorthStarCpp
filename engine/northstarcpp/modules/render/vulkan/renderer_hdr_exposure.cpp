// Temporal scene auto-exposure from a 256-byte GPU log-luminance meter.
// Readback is processed only after the previous frame fence is signaled.
#include "renderer_private.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <chrono>
namespace northstarcpp::render::vulkan {
namespace {
float half_to_float(std::uint16_t value) noexcept {
    const unsigned sign=value>>15u;
    const unsigned exponent=(value>>10u)&31u;
    const unsigned mantissa=value&1023u;
    float number=0.f;
    if(exponent==0u)number=std::ldexp(float(mantissa),-24);
    else if(exponent<31u)number=std::ldexp(float(1024u+mantissa),
                                           int(exponent)-25);
    else return 0.f; // Malformed/non-finite measurement is never trusted.
    return sign?-number:number;
}
}
void Renderer::record_hdr_luminance(VkCommandBuffer cmd) {
    if(!auto_exposure_enabled_)return;
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView=luminance_view_;
    color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent={luminance_dim_,luminance_dim_};
    rendering.layerCount=1;rendering.colorAttachmentCount=1;
    rendering.pColorAttachments=&color;
    vkCmdBeginRendering(cmd,&rendering);
    VkViewport viewport{0,0,float(luminance_dim_),
                        float(luminance_dim_),0,1};
    VkRect2D scissor{};scissor.extent={luminance_dim_,luminance_dim_};
    vkCmdSetViewport(cmd,0,1,&viewport);
    vkCmdSetScissor(cmd,0,1,&scissor);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,luminance_pipeline_);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,
                            bloom_pipeline_layout_,0,1,
                            &luminance_descriptor_set_,0,nullptr);
    vkCmdDraw(cmd,3,1,0,0);
    vkCmdEndRendering(cmd);
}
void Renderer::record_hdr_luminance_copy(VkCommandBuffer cmd) {
    if(!auto_exposure_enabled_)return;
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount=1;
    copy.imageExtent={luminance_dim_,luminance_dim_,1};
    vkCmdCopyImageToBuffer(cmd,luminance_image_,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            luminance_readback_buffer_,1,&copy);
    exposure_pending_=true;
}
void Renderer::collect_hdr_exposure() {
    if(!auto_exposure_enabled_||!exposure_pending_)return;
    // WaitForFences(completed_) has already succeeded in Renderer::draw.
    constexpr VkDeviceSize bytes=luminance_dim_*luminance_dim_*4u;
    void* address=nullptr;
    if(vkMapMemory(device_,luminance_readback_memory_,0,bytes,0,
                   &address)!=VK_SUCCESS)return;
    auto const* values=static_cast<std::uint16_t const*>(address);
    double sum=0.;
    unsigned valid=0;
    for(unsigned i=0;i<luminance_dim_*luminance_dim_;++i) {
        float log_luma=half_to_float(values[2*i]);
        if(!std::isfinite(log_luma)||log_luma< -14.f||log_luma>16.f)
            continue;
        sum+=log_luma;
        ++valid;
    }
    vkUnmapMemory(device_,luminance_readback_memory_);
    exposure_pending_=false;
    if(!valid)return;
    const float average_log2=float(sum/double(valid));
    // Exposure value 0 targets roughly 18% middle gray after manual EV.
    // Match D3D12 HDR target-exposure limits (0.25x..4x).
    const float target_ev=std::clamp(std::log2(.18f)-average_log2,
                                     -2.f,2.f);
    const auto now=std::chrono::steady_clock::now();
    if(exposure_last_frame_.time_since_epoch().count()==0) {
        adapted_exposure_ev_=target_ev;
        exposure_last_frame_=now;
        return;
    }
    float dt=1.f/60.f;
    if(exposure_last_frame_.time_since_epoch().count()!=0)
        dt=std::clamp(std::chrono::duration<float>(
            now-exposure_last_frame_).count(),1.f/240.f,.10f);
    exposure_last_frame_=now;
    const float speed=target_ev>adapted_exposure_ev_?
        exposure_speed_up_:exposure_speed_down_;
    const float alpha=1.f-std::exp(-dt*speed);
    adapted_exposure_ev_+=alpha*(target_ev-adapted_exposure_ev_);
}
} // namespace northstarcpp::render::vulkan
