// Preview decoder for original PC TLOU2 sky/cloud BCn GPU textures.
// Pure CPU; uses the already-vendored bcdec.h (no image approximation).
// HDR BC6H is tone-mapped ONLY for preview. Original BC6H DDS is retained.
#include <cstdint>
#include <cmath>
#include <cstring>
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

#if defined(_WIN32)
#define SKY_EXPORT extern "C" __declspec(dllexport)
#else
#define SKY_EXPORT extern "C"
#endif

SKY_EXPORT int sky_decode_rgba8(const std::uint8_t* source,
    std::uint32_t width, std::uint32_t height, std::uint32_t dxgi,
    std::uint8_t* out, std::uint64_t nbytes)
{
    if (!source || !out || !width || !height ||
        width > 16384 || height > 16384) return -1;
    int block_size=0;
    switch(dxgi) {
    case 71:case 72:case 80:block_size=8;break;
    case 77:case 83:case 95:case 98:case 99:block_size=16;break;
    default:return -2;
    }
    auto const bx=(width+3u)/4u, by=(height+3u)/4u;
    if (static_cast<std::uint64_t>(bx)*by*block_size!=nbytes) return -3;
    for(std::uint32_t yy=0;yy<by;++yy)
        for(std::uint32_t xx=0;xx<bx;++xx) {
            auto const* block=source+
                (static_cast<std::uint64_t>(yy)*bx+xx)*block_size;
            std::uint8_t rgba[64]{};
            switch(dxgi) {
            case 71:case 72:bcdec_bc1(block,rgba,16);break;
            case 77:bcdec_bc3(block,rgba,16);break;
            case 98:case 99:bcdec_bc7(block,rgba,16);break;
            case 80:{
                std::uint8_t r[16]{};
                bcdec_bc4(block,r,4);
                for(unsigned i=0;i<16;++i) {
                    rgba[4*i]=r[i];rgba[4*i+1]=r[i];
                    rgba[4*i+2]=r[i];rgba[4*i+3]=255;
                }
                break;
            }
            case 83:{
                std::uint8_t rg[32]{};
                bcdec_bc5(block,rg,8);
                for(unsigned i=0;i<16;++i) {
                    rgba[4*i]=rg[2*i];rgba[4*i+1]=rg[2*i+1];
                    rgba[4*i+2]=255;rgba[4*i+3]=255;
                }
                break;
            }
            case 95:{
                float rgb[48]{};
                // bcdec float destinationPitch is FLOATS, not bytes.
                bcdec_bc6h_float(block,rgb,12,0);
                for(unsigned i=0;i<16;++i) {
                    for(unsigned c=0;c<3;++c) {
                        float v=rgb[i*3+c];
                        if(!std::isfinite(v))return -4;
                        if(v<0.f)v=0.f;
                        float const mapped=std::pow(v/(1.f+v),1.f/2.2f);
                        float const bright=255.f*mapped+.5f;
                        rgba[4*i+c]=static_cast<std::uint8_t>(
                            bright>255.f?255.f:bright);
                    }
                    rgba[4*i+3]=255;
                }
                break;
            }
            default: return -2;
            }
            for(unsigned y=0;y<4;++y)
                for(unsigned x=0;x<4;++x) {
                    auto const px=xx*4+x,py=yy*4+y;
                    if(px>=width||py>=height)continue;
                    std::memcpy(out+(static_cast<std::uint64_t>(py)*width+px)*4,
                                rgba+(y*4+x)*4,4);
                }
        }
    return 0;
}
