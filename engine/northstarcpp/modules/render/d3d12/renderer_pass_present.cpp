// NorthStarCpp GPU present pass вЂ” independent translation unit.
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
        ComPtr<ID3D12Resource> motion_pixel;
        const char* const motion_probe_env=
            velocity_enabled?std::getenv("NORTHSTARCPP_D3D12_VELOCITY_PROBE"):nullptr;
        const bool motion_probe=motion_probe_env&&motion_probe_env[0]=='1'&&
                                motion_probe_env[1]==0&&frame.verify_pixels;
        UINT pitch=0;
        if(frame.verify_pixels || capture_pending) {
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
        if(motion_probe) {
            auto desc=memory(256u);
            auto heap_properties=heap(D3D12_HEAP_TYPE_READBACK);
            check(device->CreateCommittedResource(
                &heap_properties,D3D12_HEAP_FLAG_NONE,&desc,
                D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
                IID_PPV_ARGS(&motion_pixel)),
                "Allocate D3D12 motion pixel readback");
            states.transition(commands.Get(),motion_surface.Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource=motion_surface.Get();
            source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource=motion_pixel.Get();
            destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R16G16_FLOAT;
            destination.PlacedFootprint.Footprint.Width=1;
            destination.PlacedFootprint.Footprint.Height=1;
            destination.PlacedFootprint.Footprint.Depth=1;
            destination.PlacedFootprint.Footprint.RowPitch=256;
            D3D12_BOX center{width/2,height/2,0,width/2+1,height/2+1,1};
            commands->CopyTextureRegion(&destination,0,0,0,&source,&center);
            states.transition(commands.Get(),motion_surface.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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
        if(frame.verify_pixels || capture_pending) {
            wait();
            // Tonemapping/exposure can legitimately brighten the clear color,
            // so pixel thresholds alone cannot distinguish an empty scene.
            // Reject truly empty submissions independently of SDR luminance.
            if(frame.count==0 && mesh_vertices.empty() && material_draws.empty() &&
               !sky_index_count && !frame.player_max_health &&
               frame.verify_pixels!=7u && frame.verify_pixels!=8u &&
               frame.verify_pixels!=9u && frame.verify_pixels!=10u && frame.verify_pixels!=11u)
                throw std::runtime_error("GPU scene acceptance requires visible content");
            if(frame.verify_pixels)
                verify_readback(pixels.Get(),pitch,frame.verify_pixels);
            if(capture_pending) {
                if(width>3840u || height>2160u || !width || !height)
                    throw std::runtime_error("Capture dimensions exceed bounded GPU readback");
                std::vector<std::uint8_t> rgba(std::size_t(width)*height*4u);
                void* raw=nullptr;
                D3D12_RANGE read_range{0,std::size_t(pitch)*height};
                check(pixels->Map(0,&read_range,&raw),
                      "Map project thumbnail GPU readback");
                for(UINT row=0;row<height;++row){
                    std::memcpy(rgba.data()+std::size_t(row)*width*4u,
                        static_cast<const std::uint8_t*>(raw)+std::size_t(row)*pitch,
                        std::size_t(width)*4u);
                }
                D3D12_RANGE const no_writes{0,0};
                pixels->Unmap(0,&no_writes);
                capture_width=width;
                capture_height=height;
                capture_rgba.swap(rgba);
                capture_pending=false;
            }
            if(motion_probe) {
                void* mapped=nullptr;
                check(motion_pixel->Map(0,nullptr,&mapped),
                    "Map D3D12 motion pixel");
                const auto* values=static_cast<const std::uint16_t*>(mapped);
                std::fprintf(stderr,
                    "[NorthStar][D3D12][Motion] center half_bits_x=%u half_bits_y=%u\n",
                    unsigned(values[0]),unsigned(values[1]));
                motion_pixel->Unmap(0,nullptr);
            }
        }}
} // namespace northstarcpp::render::d3d12::detail
