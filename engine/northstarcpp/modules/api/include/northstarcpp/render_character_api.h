#pragma once
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_surfaces_api.h>

#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_RENDER_CHARACTER_ABI 1u
#define NORTHSTARCPP_RENDER_CHARACTER_MODULE_ABI 0x534B4E01u

typedef struct NorthStarCharacterVertex {
    float position[3];
    float normal[3];
    float uv[2];
} NorthStarCharacterVertex;
typedef struct NorthStarCharacterPart {
    const NorthStarCharacterVertex* vertices;
    uint32_t vertex_count;
    const uint32_t* indices;
    uint32_t index_count;
    NorthStarSurfaceTexture texture;
    float alpha_cutoff;
} NorthStarCharacterPart;

// Setup copies index, vertex and texture data. Pose updates copy a contiguous,
// world-space vertex buffer in the original part order.
typedef struct NorthStarRenderCharacterApi {
    uint32_t abi_version;
    int (*set_model)(void*, const NorthStarCharacterPart*, uint32_t part_count);
    int (*set_pose)(void*, const NorthStarCharacterVertex*, uint32_t vertex_count);
} NorthStarRenderCharacterApi;
#ifdef __cplusplus
}
#endif
