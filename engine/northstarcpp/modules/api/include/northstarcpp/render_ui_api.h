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

// Optional shader-driven hover extension. The core UI compositor ABI 1
// remains exactly the original 3-field table for binary compatibility.
// This separate capability is queried only if a provider implements it.
// 12 floats / 48 bytes map to root constants (D3D12) or push data (Vulkan).
#define NORTHSTARCPP_RENDER_UI_EFFECTS_ABI 1u
#define NORTHSTARCPP_RENDER_UI_EFFECTS_MODULE_ABI 0x52554601u
typedef struct NorthStarRenderUiHoverFx {
    float x, y, width, height;
    float seconds, strength, pressed, primary;
    float origin_slot, seed, layered, reserved;
} NorthStarRenderUiHoverFx;

typedef struct NorthStarRenderUiApi {
    uint32_t abi_version;
    uint32_t struct_size;
    // Calling with rgba==NULL and width=height=stride=0 disables the overlay.
    // Non-null: RGBA8 straight-alpha, top-left origin, row stride in bytes.
    // The renderer must copy the data and reject invalid dimensions/stride.
    int (*set_rgba)(void* renderer,const uint8_t* rgba,uint32_t width,
                    uint32_t height,uint32_t stride);
} NorthStarRenderUiApi;

typedef struct NorthStarRenderUiEffectsApi {
    uint32_t abi_version;
    uint32_t struct_size;
    // fx is copied immediately. NULL resets effects.
    int (*set_hover_fx)(void* renderer,const NorthStarRenderUiHoverFx* fx);
} NorthStarRenderUiEffectsApi;

// Optional project-owned UI shader program. Providers expose only the
// entrypoint they support; the engine receives bytes through its VFS and
// forwards them unchanged. No project styling is compiled into providers.
#define NORTHSTARCPP_RENDER_UI_SHADERS_ABI 1u
#define NORTHSTARCPP_RENDER_UI_SHADERS_MODULE_ABI 0x52555301u
typedef struct NorthStarRenderUiShadersApi {
    uint32_t abi_version;
    uint32_t struct_size;
    // The HLSL module defines vs/ps and owns the full UI pixel treatment.
    // D3D12 provider; NULL+0 resets to generic engine compositor.
    int (*set_hlsl)(void* renderer,const char* source,uint32_t bytes);
    // Vulkan compiled fragment SPIR-V; standard engine vertex stage stays.
    // NULL+0 resets to the generic compositor.
    int (*set_spirv)(void* renderer,const uint32_t* words,uint32_t count);
} NorthStarRenderUiShadersApi;

#ifdef __cplusplus
}
#endif
