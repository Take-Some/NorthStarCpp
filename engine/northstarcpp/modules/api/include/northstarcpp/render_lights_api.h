// NorthStarCpp optional dynamic lighting extension; stable POD C ABI.
// Light records are copied by the provider. No host memory is retained.
// No ABI layout change: global lights use reserved direction_cone.w tags.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>
#define NORTHSTARCPP_RENDER_LIGHTS_ABI 1u
#define NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI 0x524c5401u
#define NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS 256u
#define NORTHSTARCPP_LIGHT_POINT_TAG (-1.0f)
#define NORTHSTARCPP_LIGHT_DIRECTIONAL_TAG (-2.0f)
#define NORTHSTARCPP_LIGHT_SKY_TAG (-3.0f)
#ifdef __cplusplus
extern "C" {
#endif
typedef struct NorthStarRenderLight {
    float position_range[4]; // world xyz; range >0 for local light (global: ignored)
    float color_intensity[4]; // linear RGB, nonnegative physical intensity multiplier
    float direction_cone[4]; // xyz emission direction; w: -1 point, -2 directional, -3 sky, 0..1 spot
} NorthStarRenderLight;
typedef struct NorthStarRenderLightsApi {
    uint32_t abi_version;
    // count=0 releases the current light list. Not thread-safe with draw().
    int (*set_lights)(void* renderer,const NorthStarRenderLight* lights,uint32_t count);
} NorthStarRenderLightsApi;
typedef const NorthStarRenderLightsApi* (*NorthStarRenderGetLightsApi)(uint32_t requested_version);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
static_assert(sizeof(NorthStarRenderLight)==48,"Light ABI layout mismatch");
#endif
