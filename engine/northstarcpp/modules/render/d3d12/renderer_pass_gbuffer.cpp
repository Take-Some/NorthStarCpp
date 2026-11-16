// NorthStarCpp GPU gbuffer pass — independent translation unit.
#include "renderer_private.hpp"
#include "../../shared/frame_memory_counters.hpp"
namespace northstarcpp::render::d3d12::detail {
void RendererState::record_gbuffer_pass(FramePassContext& context) {
    using northstarcpp::math::float3;
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
        // Draw authored skydome before the opaque world. Its PSO disables
        // depth testing/writes, matching the existing D3D11 world order.
        if(sky_index_count) {
            ID3D12DescriptorHeap* heaps[]={sky_srv_heap.Get()};
            commands->SetDescriptorHeaps(1,heaps);
            commands->SetGraphicsRootSignature(sky_signature.Get());
            commands->SetGraphicsRootConstantBufferView(
                0,sky_camera->GetGPUVirtualAddress());
            commands->SetGraphicsRootConstantBufferView(
                1,sky_tint->GetGPUVirtualAddress());
            commands->SetGraphicsRootDescriptorTable(
                2,sky_srv_heap->GetGPUDescriptorHandleForHeapStart());
            commands->SetPipelineState(hdr_enabled?
                hdr_sky_pipeline.Get():sky_pipeline.Get());
            commands->IASetVertexBuffers(0,1,&sky_vertex_view);
            commands->IASetIndexBuffer(&sky_index_view);
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commands->DrawIndexedInstanced(sky_index_count,1,0,0,0);
        }
        draw_editor_grid(context);
        commands->SetGraphicsRootSignature(signature.Get());
        commands->SetGraphicsRootConstantBufferView(0,constant_buffer->GetGPUVirtualAddress());
        commands->IASetVertexBuffers(0,1,&vertex_view);
        if(grid) {
            commands->SetPipelineState(hdr_enabled?hdr_lines.Get():lines.Get());
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            commands->DrawInstanced(grid,1,0,0);
        }
        if(has_deferred_geometry) {
            // All opaque authored/primitive geometry shares the same
            // G-buffer/depth; skydome and editor lines remain forward.
            std::array<D3D12_CPU_DESCRIPTOR_HANDLE,4> targets{};
            auto target=deferred_rtvs->GetCPUDescriptorHandleForHeapStart();
            constexpr float empty[4]{0.f,0.f,0.f,0.f};
            for(UINT i=0;i<(gbuffer_v2?4u:3u);++i) {
                targets[i]=target;
                constexpr float neutral_aux[4]{1.f,0.f,0.f,0.f};
                commands->ClearRenderTargetView(target,i==3?neutral_aux:empty,0,nullptr);
                target.ptr+=deferred_rtv_stride;
            }
            commands->OMSetRenderTargets(gbuffer_v2?4u:3u,targets.data(),FALSE,&dsv);
            if(hud_tri_start>grid) {
                commands->SetGraphicsRootSignature(signature.Get());
                commands->SetGraphicsRootConstantBufferView(
                    0,constant_buffer->GetGPUVirtualAddress());
                commands->SetPipelineState(gbuffer_triangles.Get());
                commands->IASetVertexBuffers(0,1,&vertex_view);
                commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                commands->DrawInstanced(hud_tri_start-grid,1,grid,0);
            }
        }
        if(timestamp_heap)commands->EndQuery(timestamp_heap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,1);
        // Authored PBR pass uses a distinct provider-neutral UV vertex
        // format, one cached SRV table per material and a GGX shader.
        // Always draw this before HUD, preserving the same scene depth.
        auto const eye=frame.camera_override||frame.first_person?
            focus:focus+float3(8.f,7.f,-10.f);
        float const eye_data[4]{eye.x,eye.y,eye.z,1.f};
        if(!material_draws.empty()) {
            if(!material_vertex_data||!material_constant_data||!deferred_constant_data)
                throw std::runtime_error("D3D12 deferred buffers not initialized");
            if(material_vertices_dirty) {
                const auto bytes=material_cpu_vertices.size()*sizeof(NorthStarMaterialVertex);
                std::memcpy(material_vertex_data,material_cpu_vertices.data(),bytes);
                diagnostics::frame_memory::copied(
                    &NorthStarFrameMemoryCounters::geometry_copy_bytes,bytes);
                material_vertices_dirty=false;
            }
            ID3D12DescriptorHeap* material_heaps[]{material_srv_heap.Get()};
            commands->SetDescriptorHeaps(1,material_heaps);
            commands->SetGraphicsRootSignature(material_signature.Get());
            std::memcpy(material_camera_data,&matrix,sizeof(matrix));
            std::memcpy(material_camera_data+sizeof(matrix),eye_data,sizeof(eye_data));
            commands->SetGraphicsRootConstantBufferView(
                0,material_camera_gpu->GetGPUVirtualAddress());
            commands->SetPipelineState(material_pipeline.Get());
            commands->IASetVertexBuffers(0,1,&material_vertex_view);
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            auto base=material_srv_heap->GetGPUDescriptorHandleForHeapStart();
            for(UINT i=0;i<material_draws.size();++i) {
                auto const& draw=material_draws[i];
                if(draw.blend_mode==2)continue;
                commands->SetPipelineState(hot_pipeline(i)?hot_pipeline(i):
                (draw.variant_pipeline?
                    draw.variant_pipeline.Get():material_pipeline.Get()));
                commands->SetGraphicsRootConstantBufferView(1,
                    material_constant_gpu->GetGPUVirtualAddress()+UINT64(i)*256u);
                auto handle=base;
                handle.ptr+=UINT64(i*5)*material_descriptor_increment;
                commands->SetGraphicsRootDescriptorTable(2,handle);
                commands->DrawInstanced(draw.count,1,draw.first,0);
            }
        }
}
} // namespace northstarcpp::render::d3d12::detail
