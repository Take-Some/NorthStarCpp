// Optional NorthStar render GPU frame timestamps. Stable C ABI, no dependency
// on D3D12 objects; absence indicates a provider without GPU timing support.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>

#define NORTHSTARCPP_RENDER_TIMING_ABI 1u
#define NORTHSTARCPP_RENDER_TIMING_MODULE_ABI 0x52544d01u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NorthStarRenderTimingStats {
    uint64_t gpu_timestamp_frequency_hz;
    double total_gpu_ms;
    double opaque_gpu_ms;
    double deferred_gpu_ms;
    double postfx_gpu_ms;
    double hud_gpu_ms;
    uint32_t valid;
    uint32_t reserved;
} NorthStarRenderTimingStats;

typedef struct NorthStarRenderTimingApi {
    uint32_t abi_version;
    // This debug/profiling read waits for completion of the latest frame.
    // Do NOT call it from every production frame without budgeting the stall.
    int (*read)(void* renderer, NorthStarRenderTimingStats* result);
} NorthStarRenderTimingApi;

typedef const NorthStarRenderTimingApi*
    (*NorthStarRenderGetTimingApi)(uint32_t requested_version);

#ifdef __cplusplus
}
#endif
