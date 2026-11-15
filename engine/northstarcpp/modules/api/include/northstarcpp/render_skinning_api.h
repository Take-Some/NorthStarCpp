// NorthStarCpp: optional GPU skinning extension for engine.render.
// Eight native YDD influences are retained; no OBJ or CPU vertex expansion.
// ABI is additive and provider-replaceable; older render providers may omit it.
#pragma once
#include <northstarcpp/render_materials_api.h>
#ifdef __cplusplus
extern "C" {
#endif

#define NORTHSTARCPP_RENDER_SKINNED_MATERIALS_ABI 1u
#define NORTHSTARCPP_RENDER_SKINNED_MATERIALS_MODULE_ABI 0x534b4d01u
#define NORTHSTARCPP_SKIN_MAX_JOINTS 256u

typedef struct NorthStarSkinMaterialVertex {
    float x,y,z;
    float nx,ny,nz;
    float u,v;
    uint16_t joints[8];
    float weights[8];
} NorthStarSkinMaterialVertex;

typedef struct NorthStarSkinnedMaterialBatch {
    // Source vertices, GPU textures and material parameters. The base
    // material.vertices pointer is unused; triangle vertices are supplied
    // separately as NorthStarSkinMaterialVertex.
    NorthStarMaterialBatchV2 material;
    const NorthStarSkinMaterialVertex* vertices;
    uint32_t vertex_count;
    // Packed world-space row-major float4x4 matrices; 16 floats per joint.
    // Palettes are copied during set_batches and do not outlive the call.
    const float* palette;
    uint32_t joint_count;
} NorthStarSkinnedMaterialBatch;

typedef struct NorthStarRenderSkinnedMaterialsApi {
    uint32_t abi_version;
    int (*set_batches)(void*,const NorthStarSkinnedMaterialBatch*,uint32_t count);
} NorthStarRenderSkinnedMaterialsApi;

#ifdef __cplusplus
}
#endif
