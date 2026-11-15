// NorthStarCpp GPU present pass — independent translation unit.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d12::detail {
void RendererState::record_present_pass(FramePassContext& context) {
    auto const& frame=context.frame;
    auto const& matrix=context.matrix;
    auto const& focus=context.focus;
    auto const& camera_forward=context.camera_forward;
    auto const& csm=context.csm;
    auto const& vertices=context.vertices;
    auto const grid=context.grid;
    auto const hud_tri_start=context.hud_tri_start;
    auto const hud_line_start=context.hud_line_start;
    auto const index=context.index;
    auto const has_deferred_geometry=context.has_deferred_geometry;
    auto& states=context.states;
    auto const rtv=context.rtv;
    auto const scene_rtv=context.scene_rtv;
    auto const dsv=context.dsv;
    auto const viewport=context.viewport;
    auto const scissors=context.scissors;
        if(hud_tri_start<hud_line_start||hud_line_start<vertices.size()) {
            commands->SetGraphicsRootSignature(signature.Get());
            commands->SetGraphicsRootConstantBufferView(
                0,hud_camera->GetGPUVirtualAddress());
            if(hud_line_start>hud_tri_start) {
                commands->SetPipelineState(hud_triangles_pso.Get());
                commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                commands->DrawInstanced(hud_line_start-hud_tri_start,1,hud_tri_start,0);
            }
            if(vertices.size()>hud_line_start) {
                commands->SetPipelineState(hud_lines_pso.Get());
                commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
                commands->DrawInstanced(UINT(vertices.size())-hud_line_start,1,hud_line_start,0);
            }
        }
        // Screen-space engine.ui is composited after scene tonemapping and HUD.
        // The UI texture is independent from world lighting and depth.
        record_ui_pass(context);
        if(timestamp_heap) {
            commands->EndQuery(timestamp_heap.Get(),
                D3D12_QUERY_TYPE_TIMESTAMP,4);
            commands->ResolveQueryData(timestamp_heap.Get(),
                D3D12_QUERY_TYPE_TIMESTAMP,0,timestamp_count,
                timestamp_readback.Get(),0);
        }
        // Copy the actual rendered back buffer into a row-aligned READBACK
        // heap before transitioning it back to PRESENT.
        ComPtr<ID3D12Resource> pixels;
        UINT pitch=0;
        if(frame.verify_pixels) {
            pitch=(width*4u+255u)&~255u;
            auto buffer_desc=memory(UINT64(pitch)*height);
            auto readback_heap=heap(D3D12_HEAP_TYPE_READBACK);
            check(device->CreateCommittedResource(&readback_heap,D3D12_HEAP_FLAG_NONE,
                &buffer_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
                IID_PPV_ARGS(&pixels)),"Allocate D3D12 pixel readback");
            states.transition(commands.Get(),backs[index].Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource=backs[index].Get();
            src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource=pixels.Get();
            dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            dst.PlacedFootprint.Footprint.Width=width;
            dst.PlacedFootprint.Footprint.Height=height;
            dst.PlacedFootprint.Footprint.Depth=1;
            dst.PlacedFootprint.Footprint.RowPitch=pitch;
            commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        }
        states.transition(commands.Get(),backs[index].Get(),
            D3D12_RESOURCE_STATE_PRESENT);
        check(commands->Close(),"Close D3D12 command list");
        ID3D12CommandList* lists[]={commands.Get()};
        queue->ExecuteCommandLists(1,lists);
        check(swapchain->Present(1,0),"Present D3D12 frame");
        if(hdr_enabled) {
            exposure_history_valid=true;
            exposure_frame_index^=1u;
        }
        timestamp_pending=timestamp_heap.Get()!=nullptr && timestamp_readback.Get()!=nullptr;
        if(frame.verify_pixels) {
            wait();
            // Tonemapping/exposure can legitimately brighten the clear color,
            // so pixel thresholds alone cannot distinguish an empty scene.
            // Reject truly empty submissions independently of SDR luminance.
            if(frame.count==0 && mesh_vertices.empty() && material_draws.empty() &&
               !sky_index_count && !frame.player_max_health &&
               frame.verify_pixels!=7u && frame.verify_pixels!=8u &&
               frame.verify_pixels!=9u)
                throw std::runtime_error("GPU scene acceptance requires visible content");
            verify_readback(pixels.Get(),pitch,frame.verify_pixels);
        }}
} // namespace northstarcpp::render::d3d12::detail
