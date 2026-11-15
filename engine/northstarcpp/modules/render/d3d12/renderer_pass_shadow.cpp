// NorthStarCpp GPU shadow pass — independent translation unit.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d12::detail {
void RendererState::record_shadow_pass(FramePassContext& context) {
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
        if(shadows_enabled && has_deferred_geometry) {
            if(!sun_shadow_constants_data || !sun_shadow_map)
                throw std::runtime_error("Sun shadow resources unavailable");
            for(UINT cascade=0;cascade<cascade_count;++cascade)
                std::memcpy(sun_shadow_constants_data+cascade*256,
                    csm.cascades[cascade].matrix.data(),64);
            if(!material_draws.empty() && !material_cpu_vertices.empty())
                std::memcpy(material_vertex_data,material_cpu_vertices.data(),
                    material_cpu_vertices.size()*sizeof(NorthStarMaterialVertex));
            D3D12_VIEWPORT shadow_viewport{0.f,0.f,
                float(sun_shadow_resolution),float(sun_shadow_resolution),0.f,1.f};
            D3D12_RECT shadow_scissor{0,0,LONG(sun_shadow_resolution),
                LONG(sun_shadow_resolution)};
            commands->RSSetViewports(1,&shadow_viewport);
            commands->RSSetScissorRects(1,&shadow_scissor);
            commands->SetGraphicsRootSignature(signature.Get());
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            for(UINT cascade=0;cascade<cascade_count;++cascade) {
                auto sun_dsv=sun_shadow_dsv_heap->GetCPUDescriptorHandleForHeapStart();
                sun_dsv.ptr+=UINT64(cascade)*shadow_dsv_stride;
                commands->OMSetRenderTargets(0,nullptr,FALSE,&sun_dsv);
                commands->ClearDepthStencilView(sun_dsv,D3D12_CLEAR_FLAG_DEPTH,
                    1.f,0,0,nullptr);
                commands->SetGraphicsRootConstantBufferView(0,
                    sun_shadow_constants->GetGPUVirtualAddress()+UINT64(cascade)*256);
                if(hud_tri_start>grid) {
                    commands->SetPipelineState(shadow_geometry_pso.Get());
                    commands->IASetVertexBuffers(0,1,&vertex_view);
                    commands->DrawInstanced(hud_tri_start-grid,1,grid,0);
                }
                if(!material_draws.empty()) {
                    commands->SetPipelineState(shadow_material_pso.Get());
                    commands->IASetVertexBuffers(0,1,&material_vertex_view);
                    for(auto const& draw:material_draws) {
                        if(draw.blend_mode==2)continue;
                        commands->DrawInstanced(draw.count,1,draw.first,0);
                    }
                }
            }
            commands->RSSetViewports(1,&viewport);
            commands->RSSetScissorRects(1,&scissors);
            commands->OMSetRenderTargets(1,&scene_rtv,FALSE,&dsv);
        }
}
} // namespace northstarcpp::render::d3d12::detail
