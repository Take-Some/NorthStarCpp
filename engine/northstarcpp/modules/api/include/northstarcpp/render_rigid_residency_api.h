// Optional per-mesh GPU retirement extension. Retained alongside ABI v1.
// A batch call completes all GPU uses before destroying the requested
// vertex/index allocations. External render providers may omit this ABI.
#pragma once
#include <northstarcpp/render_rigid_instances_api.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_RENDER_RIGID_RESIDENCY_ABI 1u
#define NORTHSTARCPP_RENDER_RIGID_RESIDENCY_MODULE_ABI 0x52475202u
typedef struct NorthStarRenderRigidResidencyApi {
    uint32_t abi_version;
    uint32_t struct_size;
    // All keys must be absent from the current active instance list.
    // Returns success only after GPU access has completed and buffers freed.
    int (*release_meshes)(void* renderer,const uint64_t* keys,uint32_t count);
} NorthStarRenderRigidResidencyApi;
#ifdef __cplusplus
}
#endif
