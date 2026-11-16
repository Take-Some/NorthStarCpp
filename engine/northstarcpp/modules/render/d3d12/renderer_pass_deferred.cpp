// NorthStarCpp GPU deferred pass — independent translation unit.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d12::detail {
void RendererState::record_deferred_pass(FramePassContext& context) {
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
    auto const eye=frame.camera_override||frame.first_person?
        focus:focus+northstarcpp::math::float3(8.f,7.f,-10.f);
    float const eye_data[4]{eye.x,eye.y,eye.z,1.f};
        if(has_deferred_geometry) {
            if(!deferred_constant_data)
                throw std::runtime_error("D3D12 deferred resolve not initialized");
            // Lighting pass: sample all primitive and authored G-buffer pixels,
            // and reconstruct world position from the typed depth SRV.
            commands->OMSetRenderTargets(1,&scene_rtv,FALSE,nullptr);
            auto inverse=northstarcpp::render::inverse_view_projection(
                reinterpret_cast<float const*>(&matrix));
            std::memcpy(deferred_constant_data,inverse.data(),64);
            std::memcpy(deferred_constant_data+64,eye_data,16);
            float const screen_data[4]{float(width),float(height),
                1.f/float(width),1.f/float(height)};
            std::memcpy(deferred_constant_data+80,screen_data,16);
            // Upload conservative 32x32 light lists from the inverse camera
            // matrix. The GPU only evaluates lights visible in each tile.
            // No light may be silently removed when a tile overflows.
            auto const columns=(width+northstarcpp::render::light_tile_width-1)/
                               northstarcpp::render::light_tile_width;
            auto const rows=(height+northstarcpp::render::light_tile_width-1)/
                            northstarcpp::render::light_tile_width;
            std::uint32_t const tile_info[4]{
                columns,rows,static_cast<std::uint32_t>(dynamic_lights.size()),
                northstarcpp::render::light_tile_width};
            std::memcpy(deferred_constant_data+96,tile_info,sizeof(tile_info));
            float const ao_info[4]{ssao_enabled?1.f:0.f,1.25f,1.0f,0.04f};
            std::memcpy(deferred_constant_data+112,ao_info,sizeof(ao_info));
            float const csm_forward[4]{
                camera_forward.x,camera_forward.y,camera_forward.z,float(cascade_count)};
            std::memcpy(deferred_constant_data+128,csm_forward,sizeof(csm_forward));
            for(UINT cascade=0;cascade<cascade_count;++cascade)
                std::memcpy(deferred_constant_data+144+cascade*64,
                    csm.cascades[cascade].matrix.data(),64);
            float const shadow_info[4]{
                shadows_enabled?1.f:0.f,1.f/float(sun_shadow_resolution),
                0.0035f,pcss_enabled?1.f:0.f};
            std::memcpy(deferred_constant_data+336,shadow_info,sizeof(shadow_info));
            float const shadow_splits[4]{
                csm.cascades[0].far_distance,csm.cascades[1].far_distance,
                csm.cascades[2].far_distance,0.08f};
            std::memcpy(deferred_constant_data+352,shadow_splits,sizeof(shadow_splits));
            float const hdr_settings[4]{hdr_enabled?1.f:0.f,
                float(gbuffer_debug_mode),
                frame.camera_override && frame.camera_far_clip>0.f?
                    frame.camera_far_clip:250.f,gbuffer_v2?1.f:0.f};
            std::memcpy(deferred_constant_data+368,hdr_settings,sizeof(hdr_settings));
            float normalized_shadow_bias[4]{};
            for(UINT cascade=0;cascade<cascade_count;++cascade) {
                auto const& c=csm.cascades[cascade];
                // A texel-aware bias prevents far-cascade peter-panning from
                // a single hard-coded normalized depth offset.
                float const depth_scale=std::sqrt(
                    c.matrix[2]*c.matrix[2]+c.matrix[6]*c.matrix[6]+
                    c.matrix[10]*c.matrix[10]);
                normalized_shadow_bias[cascade]=std::clamp(
                    (0.75f*c.world_texel+0.005f)*depth_scale,0.0001f,0.003f);
            }
            std::memcpy(deferred_constant_data+384,normalized_shadow_bias,
                        sizeof(normalized_shadow_bias));
            if(!dynamic_lights.empty()) {
                if(!dynamic_light_bytes||!tile_index_bytes)
                    throw std::runtime_error("Tiled lighting upload not initialized");
                bool const dirty=tile_light_cache.update(width,height,inverse,
                    {eye.x,eye.y,eye.z},dynamic_lights.data(),
                    static_cast<std::uint32_t>(dynamic_lights.size()));
                if(dirty) {
                    auto const& grid=tile_light_cache.grid();
                    if(grid.indices.size()!=tile_buffer_entries)
                        throw std::runtime_error("Tiled lighting SRV size mismatch");
                    std::memcpy(dynamic_light_bytes,dynamic_lights.data(),
                        dynamic_lights.size()*sizeof(NorthStarRenderLight));
                    std::memcpy(tile_index_bytes,grid.indices.data(),
                        grid.indices.size()*sizeof(std::uint32_t));
                }
            }
            ID3D12DescriptorHeap* lighting_heaps[]{deferred_srvs.Get()};
            commands->SetDescriptorHeaps(1,lighting_heaps);
            commands->SetGraphicsRootSignature(deferred_signature.Get());
            commands->SetGraphicsRootConstantBufferView(
                0,deferred_constants->GetGPUVirtualAddress());
            commands->SetGraphicsRootDescriptorTable(
                1,deferred_srvs->GetGPUDescriptorHandleForHeapStart());
            commands->SetPipelineState(hdr_enabled?
                hdr_deferred_pipeline.Get():deferred_pipeline.Get());
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commands->DrawInstanced(3,1,0,0);
            commands->OMSetRenderTargets(1,&scene_rtv,FALSE,&dsv);
        }
}
} // namespace northstarcpp::render::d3d12::detail
