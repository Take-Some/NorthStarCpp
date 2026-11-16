// Optional GPU-backed editor draw-ID readback; render ABI 5 unchanged.
#pragma once
#include <stdint.h>
#define NORTHSTARCPP_RENDER_PICK_ABI 1u
#define NORTHSTARCPP_RENDER_PICK_MODULE_ABI 0x52504b01u
#define NORTHSTARCPP_RENDER_PICK_V2_ABI 2u
#define NORTHSTARCPP_RENDER_PICK_V2_MODULE_ABI 0x52504b02u
#ifdef __cplusplus
extern "C" {
#endif
typedef struct NorthStarRenderPickApi {
    uint32_t abi_version;
    uint32_t struct_size;
    // 1 means GPU read complete; -1 means no hit.
    int (*read_cube_id)(void* renderer,uint32_t x,uint32_t y,
                        int32_t* cube_draw_index);
} NorthStarRenderPickApi;
// ABI v2 supplies stable 64-bit ECS entity IDs, independent of render batches.
// Implementations COPY arrays before returning and never retain host pointers.
#define NORTHSTARCPP_RENDER_PICK_MAX_VERTICES 900000u
typedef struct NorthStarRenderPickVertex {
    float x,y,z;
    uint32_t reserved; // zero; explicit C ABI alignment
    uint64_t entity_id; // zero = unselectable
} NorthStarRenderPickVertex;
typedef struct NorthStarRenderPickApiV2 {
    uint32_t abi_version;
    uint32_t struct_size;
    int (*set_world_triangles)(void* renderer,
        const NorthStarRenderPickVertex* vertices,uint32_t count);
    int (*set_cube_entities)(void* renderer,const uint64_t* ids,
                            uint32_t count);
    int (*read_entity_id)(void* renderer,uint32_t x,uint32_t y,
                          uint64_t* entity_id);
} NorthStarRenderPickApiV2;
// Async readback extension: GPU work is submitted without waiting; poll is
// nonblocking. v1/v2 APIs and Render ABI 5 remain source/binary compatible.
#define NORTHSTARCPP_RENDER_PICK_ASYNC_ABI 3u
#define NORTHSTARCPP_RENDER_PICK_ASYNC_MODULE_ABI 0x52504b03u
#define NORTHSTARCPP_PICK_PENDING 0u
#define NORTHSTARCPP_PICK_READY 1u
#define NORTHSTARCPP_PICK_STALE 2u
#define NORTHSTARCPP_PICK_UNKNOWN 3u
typedef struct NorthStarRenderPickAsyncRequest {
    uint64_t request_id;        // strictly increasing per viewport
    uint64_t scene_generation;  // invalidated when scene/camera changes
    uint32_t x,y;               // physical swapchain client pixels
    uint32_t reserved;
} NorthStarRenderPickAsyncRequest;
typedef struct NorthStarRenderPickAsyncResult {
    uint64_t request_id;
    uint64_t scene_generation;
    uint64_t entity_id;         // zero for background
    uint32_t status;            // PENDING / READY / STALE / UNKNOWN
    uint32_t reserved;
} NorthStarRenderPickAsyncResult;
typedef struct NorthStarRenderPickAsyncApi {
    uint32_t abi_version;
    uint32_t struct_size;
    int (*enqueue)(void* renderer,NorthStarRenderPickAsyncRequest const*);
    int (*poll)(void* renderer,uint64_t request_id,
                NorthStarRenderPickAsyncResult*);
    // Cancellation is logical: in-flight GPU resources retire on their fence.
    int (*invalidate_before)(void* renderer,uint64_t minimum_scene_generation);
} NorthStarRenderPickAsyncApi;
#ifdef __cplusplus
}
#endif
