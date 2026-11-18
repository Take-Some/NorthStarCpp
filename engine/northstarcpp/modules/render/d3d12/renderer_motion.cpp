// D3D12 RG16F camera/depth motion vector pass, independent of tonemapping.
// GPU moving-object/skinned velocity overlays remain a separate next step.
#include "renderer_private.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace northstarcpp::render::d3d12::detail {
void RendererState::record_motion_pass(FramePassContext& context) {
    if(!velocity_enabled)return;
    if(!motion_surface||!motion_pipeline||!motion_constants||
       !motion_constant_data||!motion_rtv_heap)
        throw std::logic_error("D3D12 velocity resources not initialized");
    auto& states=context.states;
    auto target=motion_rtv_heap->GetCPUDescriptorHandleForHeapStart();
    commands->OMSetRenderTargets(1,&target,FALSE,nullptr);
    const float zero[4]{0.f,0.f,0.f,0.f};
    commands->ClearRenderTargetView(target,zero,0,nullptr);
    if(context.has_deferred_geometry) {
        struct MotionPush {
            float current_camera[4],current_project[4];
            float previous_camera[4],previous_project[4];
            float viewport[4],options[4];
        } push{};
        static_assert(sizeof(push)==96);
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
        fill(temporal_frame.current,push.current_camera,push.current_project);
        fill(temporal_frame.previous,push.previous_camera,push.previous_project);
        push.viewport[0]=float(width);
        push.viewport[1]=float(height);
        push.viewport[2]=1.f/float(width);
        push.viewport[3]=1.f/float(height);
        push.options[0]=temporal_frame.history_valid?1.f:0.f;
        std::memcpy(motion_constant_data,&push,sizeof(push));
        ID3D12DescriptorHeap* heaps[]{deferred_srvs.Get()};
        commands->SetDescriptorHeaps(1,heaps);
        commands->SetGraphicsRootSignature(deferred_signature.Get());
        commands->SetGraphicsRootConstantBufferView(
            0,motion_constants->GetGPUVirtualAddress());
        commands->SetGraphicsRootDescriptorTable(
            1,deferred_srvs->GetGPUDescriptorHandleForHeapStart());
        commands->SetPipelineState(motion_pipeline.Get());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->RSSetViewports(1,&context.viewport);
        commands->RSSetScissorRects(1,&context.scissors);
        commands->DrawInstanced(3,1,0,0);
    }
    states.transition(commands.Get(),motion_surface.Get(),
                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // Shader reads depth in this pass; preserve the renderer's canonical
    // end-of-frame depth-write state for subsequent frames and GPU picking.
    states.transition(commands.Get(),depth.Get(),
                      D3D12_RESOURCE_STATE_DEPTH_WRITE);
}
} // namespace northstarcpp::render::d3d12::detail
