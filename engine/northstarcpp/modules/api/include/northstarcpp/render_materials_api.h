// NorthStarCpp 2026 — provider-overridable UV/PBR material rendering ABI.
// Optional extension to engine.render ABI 5; no change to existing triangle API.
// Host retains source pointers; set_batches must copy needed vertices/texels.
#pragma once
#include <northstarcpp/render_plugin.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_RENDER_MATERIALS_ABI 1u
#define NORTHSTARCPP_RENDER_MATERIALS_MODULE_ABI 0x524d5401u
#define NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES 128u
#define NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES 300000u
#define NORTHSTARCPP_RENDER_MATERIAL_TEXTURE_SLOTS 5u
typedef struct NorthStarMaterialVertex {
    float x,y,z;
    float nx,ny,nz;
    float u,v;
} NorthStarMaterialVertex;
typedef struct NorthStarMaterialTexture {
    const uint8_t* rgba8;   // tightly packed RGBA8, top-left origin
    uint32_t width,height;
    uint32_t data_bytes;
} NorthStarMaterialTexture;
typedef struct NorthStarMaterialBatch {
    const NorthStarMaterialVertex* vertices;
    uint32_t vertex_count;  // triangles; no separate index buffer
    float base_color[4];    // linear RGB and alpha
    float metallic;         // 0..1
    float roughness;        // 0..1
    uint32_t unlit;
    uint32_t reserved;
    // 0=albedo(sRGB), 1=normal(linear), 2=roughness(linear),
    // 3=metallic(linear), 4=emissive(sRGB). Empty textures use nullptr.
    NorthStarMaterialTexture textures[NORTHSTARCPP_RENDER_MATERIAL_TEXTURE_SLOTS];
} NorthStarMaterialBatch;
typedef struct NorthStarRenderMaterialsApi {
    uint32_t abi_version;
    // Clears on count=0. Replaces all authored material draws for next frame.
    // Returning 0 means incompatibility/error; host must not silently
    // flatten textured material geometry into old colored triangles.
    int (*set_batches)(void* renderer,const NorthStarMaterialBatch* batches,
                       uint32_t count);
} NorthStarRenderMaterialsApi;
typedef const NorthStarRenderMaterialsApi*
    (*NorthStarRenderGetMaterialsApi)(uint32_t requested_version);
// ABI v2: first-class compiled Material Graph and per-instance alpha policy.
// The v1 interface remains available independently. The host supplies only
// graph expressions generated from whitelisted graph nodes; providers must
// independently validate bounded text and compile/cache variants.
#define NORTHSTARCPP_RENDER_MATERIALS_V2_ABI 2u
#define NORTHSTARCPP_RENDER_MATERIALS_V2_MODULE_ABI 0x524d5402u
typedef struct NorthStarMaterialBatchV2 {
    NorthStarMaterialBatch material;
    const char* graph_hlsl_defines;
    uint32_t graph_hlsl_bytes;
    uint32_t blend_mode; // 0=opaque, 1=masked, 2=translucent
    float alpha_cutoff;
    float opacity;
    uint32_t reserved[2];
} NorthStarMaterialBatchV2;
typedef struct NorthStarRenderMaterialsApiV2 {
    uint32_t abi_version;
    int (*set_batches_v2)(void* renderer,const NorthStarMaterialBatchV2* batches,
                          uint32_t count);
} NorthStarRenderMaterialsApiV2;
typedef const NorthStarRenderMaterialsApiV2*
    (*NorthStarRenderGetMaterialsApiV2)(uint32_t requested_version);
#ifdef __cplusplus
}
#endif
