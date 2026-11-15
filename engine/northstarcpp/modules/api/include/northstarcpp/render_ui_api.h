// Optional engine.render UI overlay ABI. Separate from render ABI 5.
// Host owns pixels for duration of set_rgba call; renderer copies immediately.
// Texture is sampled in the final, post-tonemap 2D UI pass.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORTHSTARCPP_RENDER_UI_ABI 1u
#define NORTHSTARCPP_RENDER_UI_MODULE_ABI 0x52554901u

typedef struct NorthStarRenderUiApi {
    uint32_t abi_version;
    uint32_t struct_size;
    // Calling with rgba==NULL and width=height=stride=0 disables the overlay.
    // Non-null: RGBA8 straight-alpha, top-left origin, row stride in bytes.
    // The renderer must copy the data and reject invalid dimensions/stride.
    int (*set_rgba)(void* renderer,const uint8_t* rgba,uint32_t width,
                    uint32_t height,uint32_t stride);
} NorthStarRenderUiApi;

#ifdef __cplusplus
}
#endif
