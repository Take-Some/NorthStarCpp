// Provider-neutral GPU Sky uniforms; ABI v1 additive extension.
// No per-frame texture recreation: only 80 bytes of simulation data change.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_RENDER_SKY_ABI 1u
#define NORTHSTARCPP_RENDER_SKY_MODULE_ABI 0x534b5901u

typedef struct NorthStarGpuSkyUniforms {
    float sun_direction[4];    // xyz unit vector, w authored aerial fog density
    float moon_direction[4];   // xyz unit vector, w unused
    float atmosphere[4];      // daylight, haze, sun intensity, moon intensity
    float cloud[4];           // coverage, speed, phase [0,1), moon illumination
    float flags[4];           // enabled, sun enabled, moon enabled, cloud enabled
    // Additive ABI v1 tail: authoritative camera origin for world-anchored
    // volumetric density. Older providers can consume the first 80 bytes.
    float camera_world[4];    // xyz metres; w reserved
} NorthStarGpuSkyUniforms;

typedef struct NorthStarRenderSkyApi {
    uint32_t abi_version;
    uint32_t struct_size;
    // May be called every game frame; validates and copies fixed-size uniforms.
    // Does not create/replace textures or wait for GPU queues.
    int (*set_uniforms)(void* renderer,const NorthStarGpuSkyUniforms* uniforms);
} NorthStarRenderSkyApi;
#ifdef __cplusplus
}
#endif
