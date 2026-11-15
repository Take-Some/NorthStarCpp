// NorthStarCpp optional dynamic lighting extension; stable POD C ABI.
// Light records are copied by the provider. No host memory is retained.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>
#define NORTHSTARCPP_RENDER_LIGHTS_ABI 1u
#define NORTHSTARCPP_RENDER_LIGHTS_MODULE_ABI 0x524c5401u
#define NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS 256u
#ifdef __cplusplus
extern "C" {
#endif
typedef struct NorthStarRenderLight {
    float position_range[4]; // world xyz, positive attenuation range
    float color_intensity[4]; // linear RGB, nonnegative physical intensity multiplier
    float direction_cone[4]; // normalized direction xyz, cosine >=0 for spot; -1 for point
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
