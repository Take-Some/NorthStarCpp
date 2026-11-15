// Provider-advertised renderer features. Optional extension to engine.render
// ABI 5; the base API and all existing extension table layouts are unchanged.
// Bits describe compiled functionality, not driver support, runtime enablement,
// visual acceptance or performance guarantees.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>
#define NORTHSTARCPP_RENDER_FEATURES_ABI 1u
#define NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI 0x52465001u

#define NS_RENDER_FEATURE_COLORED_GEOMETRY (UINT64_C(1)<<0)
#define NS_RENDER_FEATURE_TEXTURED_MATERIALS (UINT64_C(1)<<1)
#define NS_RENDER_FEATURE_MATERIAL_GRAPH (UINT64_C(1)<<2)
#define NS_RENDER_FEATURE_GPU_SKINNING (UINT64_C(1)<<3)
#define NS_RENDER_FEATURE_WORLD_SURFACES (UINT64_C(1)<<4)
#define NS_RENDER_FEATURE_CHARACTER (UINT64_C(1)<<5)
#define NS_RENDER_FEATURE_DYNAMIC_LIGHTS (UINT64_C(1)<<6)
#define NS_RENDER_FEATURE_GBUFFER (UINT64_C(1)<<7)
#define NS_RENDER_FEATURE_DEFERRED (UINT64_C(1)<<8)
#define NS_RENDER_FEATURE_CSM (UINT64_C(1)<<9)
#define NS_RENDER_FEATURE_PCSS (UINT64_C(1)<<10)
#define NS_RENDER_FEATURE_SSAO (UINT64_C(1)<<11)
#define NS_RENDER_FEATURE_HDR (UINT64_C(1)<<12)
#define NS_RENDER_FEATURE_BLOOM (UINT64_C(1)<<13)
#define NS_RENDER_FEATURE_EXPOSURE (UINT64_C(1)<<14)
#define NS_RENDER_FEATURE_TONEMAP (UINT64_C(1)<<15)
#define NS_RENDER_FEATURE_FXAA (UINT64_C(1)<<16)
#define NS_RENDER_FEATURE_GPU_TIMING (UINT64_C(1)<<17)
#define NS_RENDER_FEATURE_GPU_TRANSIENT_ALIASING (UINT64_C(1)<<18)

#ifdef __cplusplus
extern "C" {
#endif
typedef struct NorthStarRenderFeaturesApi {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t implemented_features;
} NorthStarRenderFeaturesApi;
#ifdef __cplusplus
}
static_assert(sizeof(NorthStarRenderFeaturesApi)==16,
              "Render feature ABI must remain a fixed 16-byte POD");
#endif
