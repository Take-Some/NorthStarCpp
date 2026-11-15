// NorthStarCpp UI compositor reference implementation.
// Contract for pixel-accurate GPU tests; not a GPU implementation.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace northstarcpp::ui {
struct CpuImageView {
    std::uint8_t* bytes = nullptr;
    std::uint32_t width = 0, height = 0, stride = 0;
};
struct CpuConstImageView {
    const std::uint8_t* bytes = nullptr;
    std::uint32_t width = 0, height = 0, stride = 0;
};
inline bool valid(CpuConstImageView image) noexcept {
    return image.bytes && image.width && image.height &&
        image.width <= 8192 && image.height <= 8192 &&
        image.stride >= static_cast<std::uint64_t>(image.width)*4 &&
        static_cast<std::uint64_t>(image.stride)*image.height <= 512ull*1024*1024;
}
inline bool valid(CpuImageView image) noexcept {
    return valid(CpuConstImageView{image.bytes,image.width,image.height,image.stride});
}
// Straight-alpha RGBA8 source over opaque destination.
// Supports padded scanlines, rejects mismatched dimensions and aliasing.
// Destination alpha remains 255; the GPU shader should apply the equivalent
// src.rgb * src.a + dst.rgb * (1 - src.a) equation in linear color space.
inline bool composite_opaque_rgba(CpuImageView target,
                                  CpuConstImageView overlay) noexcept {
    if (!valid(target)||!valid(overlay) ||
        target.width!=overlay.width || target.height!=overlay.height ||
        static_cast<const void*>(target.bytes)==overlay.bytes)
        return false;
    for(std::uint32_t y=0;y<target.height;++y) {
        auto* dst=target.bytes+static_cast<std::size_t>(target.stride)*y;
        auto const* src=overlay.bytes+static_cast<std::size_t>(overlay.stride)*y;
        for(std::uint32_t x=0;x<target.width;++x) {
            const std::uint32_t a=src[3];
            for(int c=0;c<3;++c)
                dst[c]=static_cast<std::uint8_t>(
                    (static_cast<std::uint32_t>(src[c])*a+
                     static_cast<std::uint32_t>(dst[c])*(255-a)+127)/255);
            dst[3]=255;
            src+=4;dst+=4;
        }
    }
    return true;
}
} // namespace northstarcpp::ui
