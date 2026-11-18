// Decode source BC6H HDR blocks to float RGB using vendor bcdec.
// No texture alteration except decompression. Pure CPU; no external DLLs.
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"
#if defined(_WIN32)
#define NS_EXPORT extern "C" __declspec(dllexport)
#else
#define NS_EXPORT extern "C" __attribute__((visibility("default")))
#endif
NS_EXPORT int ns_tlou_bc6h_decompress(const std::uint8_t* source,
                                      std::uint32_t width,std::uint32_t height,
                                      float* dst,std::uint64_t source_size) {
    if (!source || !dst || width==0 || height==0 ||
        width>16384 || height>16384) return -1;
    auto const blocks_x=(width+3u)/4u;
    auto const blocks_y=(height+3u)/4u;
    if (static_cast<std::uint64_t>(blocks_x)*blocks_y*16u!=source_size) return -2;
    for (std::uint32_t by=0;by<blocks_y;++by) {
        for (std::uint32_t bx=0;bx<blocks_x;++bx) {
            float decoded[4u*4u*3u]{};
            bcdec_bc6h_float(source+16u*(static_cast<std::uint64_t>(by)*blocks_x+bx),
                             decoded,4*3,0);
            for(std::uint32_t y=0;y<4;++y) {
                for(std::uint32_t x=0;x<4;++x) {
                    auto px=bx*4+x,py=by*4+y;
                    if(px>=width || py>=height)continue;
                    auto const* src=&decoded[(y*4+x)*3u];
                    auto* out=&dst[(static_cast<std::uint64_t>(py)*width+px)*3u];
                    for(std::uint32_t c=0;c<3;++c) {
                        if(!std::isfinite(src[c]) || src[c]<0.f) return -3;
                        out[c]=src[c];
                    }
                }
            }
        }
    }
    return 0;
}
