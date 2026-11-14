// NorthStarCpp — optional world-surface texture extension for engine.render.
// ABI 5 remains unchanged. All texture bytes belong to the host and are
// copied into provider-owned GPU resources during set_texture().
#pragma once
#include <northstarcpp/render_plugin.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_RENDER_SURFACES_ABI 1u
#define NORTHSTARCPP_RENDER_SURFACES_MODULE_ABI 0x52534601u

// BC1 = 102, BC5 = 105. No implicit BCn decompression or disk I/O.
// Mip offsets are into a tightly packed buffer; no absolute file offsets.
typedef struct NorthStarSurfaceMip {
    uint32_t width;
    uint32_t height;
    uint32_t offset;
    uint32_t bytes;
} NorthStarSurfaceMip;
typedef struct NorthStarSurfaceTexture {
    uint32_t slot;       // 0..8: three triplets of albedo, normal, roughness
    uint32_t format_id;  // native NETD 102 BC1, 105 BC5
    uint32_t srgb;
    uint32_t mip_count;
    const NorthStarSurfaceMip* mips;
    const uint8_t* data;
    uint32_t data_bytes;
} NorthStarSurfaceTexture;
typedef struct NorthStarRenderSurfacesApi {
    uint32_t abi_version;
    // Validates and copies the source texture into GPU resources; nullptr
    // clears the chosen slot. Must never retain source host pointers.
    int (*set_texture)(void* renderer, const NorthStarSurfaceTexture* texture);
    // Per-NorthStarRenderFrame.cubes surface IDs: 0=untextured, 1=sand,
    // 2=rock+moss, 3=forest floor. Count must match the next frame.count.
    int (*set_cube_surfaces)(void* renderer, const uint32_t* ids, uint32_t count);
} NorthStarRenderSurfacesApi;
#ifdef __cplusplus
}
#endif
