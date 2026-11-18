// Typed GPU PostFX operations, scheduled independently by Render Graph.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d12::detail {
void RendererState::record_translucency_pass(FramePassContext& context) {
        // Transparent materials must be composited against the opaque
        // scene before bloom/FXAA. They continue to use their forward PSOs
        // with depth testing (but no G-buffer writes).
        if(context.has_deferred_geometry) {
            auto readonly_dsv=dsvs->GetCPUDescriptorHandleForHeapStart();
            readonly_dsv.ptr+=device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            commands->OMSetRenderTargets(1,&context.scene_rtv,FALSE,
                                         &readonly_dsv);
        }
        draw_translucent_materials(context.focus,context.camera_forward);
        if(timestamp_heap)commands->EndQuery(timestamp_heap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,2);
        // Scene post-process (FXAA) runs after all 3D passes and before the
        // native HUD. Never apply AA/blur to text, icons or the reticle.
}
void RendererState::record_bloom_pass(FramePassContext& context) {
    if(!postfx_enabled())return;
    auto& states=context.states;
    auto const viewport=context.viewport;
    auto const scissors=context.scissors;
            states.transition(commands.Get(),
                hdr_enabled?hdr_scene_color.Get():scene_color.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            float const screen_data[4]{float(width),float(height),
                1.f/float(width),1.f/float(height)};
            std::memcpy(fxaa_constant_data,screen_data,sizeof(screen_data));
            float const bloom_params[4]{.60f,bloom_enabled?1.f:0.f,
                hdr_enabled?1.0f:.68f,fxaa_enabled?1.f:0.f};
            std::memcpy(fxaa_constant_data+16,bloom_params,sizeof(bloom_params));
            // Time-based eye adaptation instead of frame-count smoothing.
            auto const now=std::chrono::steady_clock::now();
            float frame_dt=1.f/60.f;
            if(exposure_frame_clock_valid)
                frame_dt=std::chrono::duration<float>(
                    now-exposure_frame_clock).count();
            exposure_frame_clock=now;
            exposure_frame_clock_valid=true;
            float const exposure_params[4]{exposure_history_valid?1.f:0.f,
                std::clamp(frame_dt,0.f,.20f),1.4f,.35f};
            std::memcpy(fxaa_constant_data+32,exposure_params,sizeof(exposure_params));
            // One 256-byte CBV allocation shared across FXAA and HDR shaders.
            // Identity look is bitwise neutral; no effects are mandatory.
            float const film_params[4]{
                film_look.film_grain,film_look.vignette,
                film_look.chromatic_pixels,film_look.saturation};
            float const film_extra[4]{
                film_look.contrast,film_look.exposure_ev,
                hdr_enabled?1.f:0.f,float((film_frame++)%4096u)};
            std::memcpy(fxaa_constant_data+48,film_params,sizeof(film_params));
            std::memcpy(fxaa_constant_data+64,film_extra,sizeof(film_extra));
            ID3D12DescriptorHeap* heaps[]{scene_srv_heap.Get()};
            commands->SetDescriptorHeaps(1,heaps);
            commands->SetGraphicsRootSignature(fxaa_signature.Get());
            commands->SetGraphicsRootConstantBufferView(
                0,fxaa_constants->GetGPUVirtualAddress());
            commands->SetGraphicsRootDescriptorTable(
                1,scene_srv_heap->GetGPUDescriptorHandleForHeapStart());
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            if(bloom_enabled) {
                if(hdr_enabled) {
                    ID3D12DescriptorHeap* hdr_heaps[]{hdr_bloom_srvs.Get()};
                    commands->SetDescriptorHeaps(1,hdr_heaps);
                    commands->SetGraphicsRootDescriptorTable(
                        1,hdr_bloom_srvs->GetGPUDescriptorHandleForHeapStart());
                }
                states.transition(commands.Get(),bloom_surface.Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
                auto bloom_rtv=bloom_rtv_heap->GetCPUDescriptorHandleForHeapStart();
                commands->OMSetRenderTargets(1,&bloom_rtv,FALSE,nullptr);
                D3D12_VIEWPORT bloom_viewport{0.f,0.f,
                    float(std::max<UINT>(1,width/4)),float(std::max<UINT>(1,height/4)),
                    0.f,1.f};
                D3D12_RECT bloom_scissor{0,0,
                    LONG(std::max<UINT>(1,width/4)),LONG(std::max<UINT>(1,height/4))};
                commands->RSSetViewports(1,&bloom_viewport);
                commands->RSSetScissorRects(1,&bloom_scissor);
                commands->SetPipelineState(bloom_pipeline.Get());
                commands->DrawInstanced(3,1,0,0);
                states.transition(commands.Get(),bloom_surface.Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                commands->RSSetViewports(1,&viewport);
                commands->RSSetScissorRects(1,&scissors);
            }
}
void RendererState::record_exposure_pass(FramePassContext& context) {
    if(!hdr_enabled)return;
    auto& states=context.states;
                // HDR scene is now SRV. Derive exposure on GPU into a 1x1
                // FP16 target, then run an independent ACES tonemapping pass
                // from linear FP16 scene/bloom into the LDR scene target.
                const UINT write_exposure=exposure_frame_index&1u;
                states.transition(commands.Get(),hdr_exposure[write_exposure].Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
                auto exposure_rtv=hdr_exposure_rtv_heap->GetCPUDescriptorHandleForHeapStart();
                exposure_rtv.ptr+=UINT64(write_exposure)*hdr_exposure_rtv_stride;
                D3D12_VIEWPORT exposure_viewport{0,0,1,1,0,1};
                D3D12_RECT exposure_scissor{0,0,1,1};
                commands->RSSetViewports(1,&exposure_viewport);
                commands->RSSetScissorRects(1,&exposure_scissor);
                commands->OMSetRenderTargets(1,&exposure_rtv,FALSE,nullptr);
                ID3D12DescriptorHeap* exposure_heaps[]{
                    hdr_exposure_srvs[write_exposure].Get()};
                commands->SetDescriptorHeaps(1,exposure_heaps);
                commands->SetGraphicsRootDescriptorTable(
                    1,hdr_exposure_srvs[write_exposure]->GetGPUDescriptorHandleForHeapStart());
                commands->SetPipelineState(hdr_exposure_pipeline.Get());
                commands->DrawInstanced(3,1,0,0);
                states.transition(commands.Get(),hdr_exposure[write_exposure].Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
void RendererState::record_tonemap_pass(FramePassContext& context) {
    if(!hdr_enabled)return;
    auto& states=context.states;
    auto const viewport=context.viewport;
    auto const scissors=context.scissors;
    const UINT write_exposure=exposure_frame_index&1u;
                states.transition(commands.Get(),scene_color.Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
                auto ldr_scene_rtv=scene_rtv_heap->GetCPUDescriptorHandleForHeapStart();
                commands->RSSetViewports(1,&viewport);
                commands->RSSetScissorRects(1,&scissors);
                commands->OMSetRenderTargets(1,&ldr_scene_rtv,FALSE,nullptr);
                ID3D12DescriptorHeap* tonemap_heaps[]{hdr_srvs[write_exposure].Get()};
                commands->SetDescriptorHeaps(1,tonemap_heaps);
                commands->SetGraphicsRootDescriptorTable(
                    1,hdr_srvs[write_exposure]->GetGPUDescriptorHandleForHeapStart());
                commands->SetPipelineState(hdr_tonemap_pipeline.Get());
                commands->DrawInstanced(3,1,0,0);
                states.transition(commands.Get(),scene_color.Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                // FXAA is a separate LDR pass after tonemapping.
                ID3D12DescriptorHeap* ldr_heaps[]{scene_srv_heap.Get()};
                commands->SetDescriptorHeaps(1,ldr_heaps);
                commands->SetGraphicsRootDescriptorTable(
                    1,scene_srv_heap->GetGPUDescriptorHandleForHeapStart());
                float ldr_params[4]{.60f,0.f,.68f,fxaa_enabled?1.f:0.f};
                std::memcpy(fxaa_constant_data+16,ldr_params,sizeof(ldr_params));
}
void RendererState::record_composite_pass(FramePassContext& context) {
    if(postfx_enabled()) {
    auto& states=context.states;
    auto const index=context.index;
    auto const rtv=context.rtv;
            if(!fxaa_enabled && !northstarcpp::render::active_postfx(film_look)) {
                // Exact, unfiltered scene copy. Bloom is added in a later
                // fullscreen overlay pass; no intermediate shader replaces
                // Scene Color when anti-aliasing is explicitly disabled.
                states.transition(commands.Get(),scene_color.Get(),
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
                states.transition(commands.Get(),backs[index].Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST);
                commands->CopyResource(backs[index].Get(),scene_color.Get());
                states.transition(commands.Get(),scene_color.Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                states.transition(commands.Get(),backs[index].Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
            commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
            if(fxaa_enabled || northstarcpp::render::active_postfx(film_look) ||
               (bloom_enabled && !hdr_enabled)) {
                commands->SetPipelineState(
                    (fxaa_enabled || northstarcpp::render::active_postfx(film_look))?
                        fxaa_pipeline.Get():bloom_composite_pipeline.Get());
                commands->DrawInstanced(3,1,0,0);
            }
    }
        if(timestamp_heap)commands->EndQuery(timestamp_heap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,3);
}
} // namespace northstarcpp::render::d3d12::detail
