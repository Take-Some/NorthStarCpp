// Diagnostics subsystem: independent D3D12 implementation.
#include "renderer_private.hpp"
#include "../shared/gpu_capture_ppm.hpp"
namespace northstarcpp::render::d3d12::detail {
NorthStarRenderTimingStats RendererState::timing_stats(){
        NorthStarRenderTimingStats stats{};
        stats.gpu_timestamp_frequency_hz=timestamp_frequency;
        if(!timestamp_heap||!timestamp_readback||!timestamp_pending||!timestamp_frequency)
            return stats;
        // This query explicitly synchronizes; keep it diagnostic rather
        // than adding an unconditional GPU stall in the main frame loop.
        wait();
        UINT64 ticks[timestamp_count]{};
        void* mapped=nullptr;
        D3D12_RANGE read_range{0,sizeof(ticks)};
        check(timestamp_readback->Map(0,&read_range,&mapped),
            "Map GPU timestamp readback");
        std::memcpy(ticks,mapped,sizeof(ticks));
        D3D12_RANGE no_writes{0,0};
        timestamp_readback->Unmap(0,&no_writes);
        for(UINT i=1;i<timestamp_count;++i)
            if(ticks[i]<ticks[i-1])return stats;
        double const scale=1000.0/static_cast<double>(timestamp_frequency);
        stats.total_gpu_ms=static_cast<double>(ticks[4]-ticks[0])*scale;
        stats.opaque_gpu_ms=static_cast<double>(ticks[1]-ticks[0])*scale;
        stats.deferred_gpu_ms=static_cast<double>(ticks[2]-ticks[1])*scale;
        stats.postfx_gpu_ms=static_cast<double>(ticks[3]-ticks[2])*scale;
        stats.hud_gpu_ms=static_cast<double>(ticks[4]-ticks[3])*scale;
        stats.valid=1u;
        return stats;
    }
NorthStarRenderMemoryStats RendererState::memory_stats() const{
        NorthStarRenderMemoryStats stats{};
        stats.vertex_buffer_bytes=UINT64(capacity)*sizeof(Vertex);
        stats.world_vertex_capacity=capacity;
        stats.sky_geometry_and_texture_bytes=sky_gpu_bytes;
        return stats;
    }

    // Genuine DXGI back-buffer inspection. A fence alone is not pixel verification.
void RendererState::verify_readback(ID3D12Resource* pixels,UINT pitch,UINT mode){
        void* data=nullptr;
        D3D12_RANGE range{0,SIZE_T(pitch)*height};
        check(pixels->Map(0,&range,&data),"Map D3D12 GPU readback");
        UINT64 colored=0,sky_samples=0,sky_pixels=0;
        bool overlay_center_ok=false;
        UINT64 material_red=0,material_green=0;
        UINT64 grid_pixels=0;
        auto const* bytes=static_cast<const uint8_t*>(data);
        northstarcpp::render::diagnostics::capture_ppm_if_requested(
            bytes,width,height,pitch,false,mode);
        for(UINT y=0;y<height;++y) {
            auto const* row=bytes+SIZE_T(y)*pitch;
            for(UINT x=0;x<width;++x) {
                auto const* pixel=row+4u*x;
                if(pixel[0]>110||pixel[1]>110||pixel[2]>110)++colored;
                if(mode==9u && x==width/2 && y==height/2)
                    overlay_center_ok=pixel[0]>200 && pixel[1]<80 && pixel[2]<80;
                if((mode==7u||mode==8u) &&
                   pixel[1]>33 && pixel[2]>40)++grid_pixels;
                if((mode==2u||mode==3u) && !(x%2) && !(y%2)) {
                    if(pixel[0]>130 && pixel[1]<90 && pixel[2]<90)
                        ++material_red;
                    if(pixel[1]>130 && pixel[0]<90 && pixel[2]<90)
                        ++material_green;
                }
                if(sky_index_count&&y<height/5&&x>width/3&&x<width*2/3) {
                    ++sky_samples;
                    if(pixel[0]>62||pixel[1]>62||pixel[2]>62)++sky_pixels;
                }
            }
        }
        D3D12_RANGE no_writes{0,0};
        pixels->Unmap(0,&no_writes);
        if(mode==2u &&
           (material_draws.empty()||material_red<100||material_green<100))
            throw std::runtime_error(
                "D3D12 material GPU readback lacks two sampled UV texture colors");
        if(mode==3u &&
           (material_draws.empty()||material_green<100||material_red>25))
            throw std::runtime_error(
                "D3D12 programmable Material Graph GPU tint was not applied");
        if(mode==9u && (!ui_width||!ui_height||!overlay_center_ok))
            throw std::runtime_error(
                "D3D12 GPU engine.ui overlay pixel readback mismatch");
        if(mode==7u && grid_pixels<180)
            throw std::runtime_error(
                "D3D12 analytic grid missing in GPU pixel readback");
        if(mode==8u && grid_pixels>12)
            throw std::runtime_error(
                "D3D12 analytic grid visible after disable");
        if(mode!=7u && mode!=8u && colored<100)throw std::runtime_error(
            "D3D12 back-buffer readback contains no visible scene geometry");
        if(sky_index_count&&(!sky_samples||sky_pixels*10<sky_samples))
            throw std::runtime_error("D3D12 skydome readback lacks sky coverage");
    }

} // namespace northstarcpp::render::d3d12::detail
