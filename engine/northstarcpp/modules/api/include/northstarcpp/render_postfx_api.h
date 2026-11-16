// NorthStar optional PostFX provider extension. Engine Render ABI 5 stays unchanged.
// A provider owns its render targets, GPU resources, and validation.
// This is original NorthStar code and does not depend on reference renderer SDKs.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_RENDER_POSTFX_ABI 1u
#define NORTHSTARCPP_RENDER_POSTFX_MODULE_ABI 0x50584601u
typedef struct NorthStarRenderPostFxSettings {
    uint32_t struct_size;
    float exposure_ev;       // [-5, 5], 0 is neutral
    float contrast;          // [0, 2], 1 is neutral
    float saturation;        // [0, 2], 1 is neutral
    float vignette;          // [0, 1], 0 is disabled
    float chromatic_pixels;  // [0, 5], 0 is disabled
    float film_grain;        // [0, 0.2], 0 is disabled
    uint32_t reserved;       // must be zero for ABI v1
} NorthStarRenderPostFxSettings;
typedef struct NorthStarRenderPostFxApi {
    uint32_t abi_version;
    uint32_t struct_size;
    // This extension is always provider-scoped; it changes no engine-global state.
    // Rejection leaves the previous look unchanged.
    int (*set_settings)(void* renderer,const NorthStarRenderPostFxSettings*);
    int (*get_settings)(void* renderer,NorthStarRenderPostFxSettings*);
} NorthStarRenderPostFxApi;
#ifdef __cplusplus
}
static_assert(sizeof(NorthStarRenderPostFxSettings)==32);
#endif
