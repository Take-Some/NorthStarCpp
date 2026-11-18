#pragma once
// Opt-in GPU-frame diagnostic PPM dump. Never uploads imagery; writes only
// when the caller explicitly supplies NORTHSTARCPP_GPU_CAPTURE_PPM.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
namespace northstarcpp::render::diagnostics {
inline void capture_ppm_if_requested(const std::uint8_t* pixels,
    std::uint32_t width,std::uint32_t height,std::size_t stride,
    bool bgra,unsigned verify_mode) noexcept {
    const char* capture_mode=std::getenv("NORTHSTARCPP_GPU_CAPTURE_MODE");
    const unsigned requested_mode=capture_mode && capture_mode[0]=='1' &&
        capture_mode[1]=='0' && capture_mode[2]==0 ? 10u:
        (capture_mode && capture_mode[0]=='1' &&
         capture_mode[1]=='1' && capture_mode[2]==0 ? 11u:1u);
    if(!pixels||!width||!height||verify_mode!=requested_mode)return;
    const char* const requested=std::getenv("NORTHSTARCPP_GPU_CAPTURE_PPM");
    if(!requested||!*requested)return;
    static std::atomic<bool> captured{false};
    bool expected=false;
    if(!captured.compare_exchange_strong(expected,true))return;
    try {
        const std::filesystem::path target(requested);
        if(target.extension()!=".ppm")return;
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(),error);
        if(error)return;
        const auto temporary=target.string()+".tmp";
        std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
        if(!stream)return;
        stream<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for(std::uint32_t y=0;y<height;++y) {
            const auto* row=pixels+std::size_t(y)*stride;
            for(std::uint32_t x=0;x<width;++x) {
                const auto* pixel=row+std::size_t(x)*4u;
                char rgb[3]{
                    char(pixel[bgra?2:0]),char(pixel[1]),
                    char(pixel[bgra?0:2])};
                stream.write(rgb,3);
            }
        }
        stream.close();
        if(!stream.good())return;
        std::filesystem::remove(target,error);
        std::filesystem::rename(temporary,target,error);
    }catch(...) {
        // Diagnostics must never affect graphics success/failure semantics.
    }
}
}
