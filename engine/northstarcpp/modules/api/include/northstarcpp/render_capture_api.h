// Optional engine.render GPU frame capture extension; no desktop screenshots.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_RENDER_CAPTURE_ABI 1u
#define NORTHSTARCPP_RENDER_CAPTURE_MODULE_ABI 0x52435001u
typedef struct NorthStarRenderCaptureApi {
    uint32_t abi_version;
    uint32_t struct_size;
    int (*request)(void* renderer);
    // First call: pixels=nullptr and capacity=0 to query width and height.
    // Second call copies packed RGBA8 width*height*4 bytes.
    int (*read)(void* renderer,uint8_t* pixels,uint32_t capacity,
                uint32_t* width,uint32_t* height);
} NorthStarRenderCaptureApi;
#ifdef __cplusplus
}
#endif
