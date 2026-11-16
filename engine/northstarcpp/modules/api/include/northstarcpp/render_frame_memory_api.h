#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORTHSTARCPP_RENDER_FRAME_MEMORY_ABI 1u
#define NORTHSTARCPP_RENDER_FRAME_MEMORY_MODULE_ABI 0x52464D01u

typedef struct NorthStarFrameMemoryCounters {
    uint64_t allocations;
    uint64_t allocation_bytes;
    uint64_t geometry_copy_bytes;
    uint64_t texture_copy_bytes;
    uint64_t palette_copy_bytes;
    uint64_t device_allocations;
    uint64_t device_allocation_bytes;
} NorthStarFrameMemoryCounters;

typedef struct NorthStarRenderFrameMemoryApi {
    uint32_t abi_version;
    // Exchange the optional sink for this calling thread. The caller owns
    // its lifetime and restores the previous sink before unwinding.
    NorthStarFrameMemoryCounters* (*exchange)(NorthStarFrameMemoryCounters*);
} NorthStarRenderFrameMemoryApi;

#ifdef __cplusplus
}
#endif
