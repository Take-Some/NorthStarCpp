// NorthStarCpp: optional, provider-independent temporal instance history ABI.
// Does not change engine.render ABI 5 or existing material / skinning extensions.
#pragma once
#include <northstarcpp/render_plugin.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_RENDER_TEMPORAL_INSTANCES_ABI 1u
#define NORTHSTARCPP_RENDER_TEMPORAL_INSTANCES_MODULE_ABI 0x544d4901u
#define NORTHSTARCPP_TEMPORAL_INSTANCE_TELEPORT 1u
#define NORTHSTARCPP_TEMPORAL_INSTANCE_MAX_JOINTS 256u
typedef struct NorthStarRenderTemporalInstance {
    // Stable ECS/entity instance ID, never a transient material-batch index.
    // Zero is invalid. Must be unique in each submitted instance set.
    uint64_t instance_id;
    // World-space row-vector 4x4 transform, 16 floats; translation [12..14].
    float world_transform[16];
    // Optional packed world-space bone palette with joint_count matrices.
    // Copied by provider; pointers remain owned by the caller.
    const float* bone_palette;
    uint32_t joint_count;
    // Teleport/discontinuous animation explicitly invalidates prior pose.
    uint32_t flags;
    // Revision of mesh topology/rig binding; change invalidates prior pose.
    uint64_t topology_revision;
} NorthStarRenderTemporalInstance;

typedef struct NorthStarRenderTemporalInstancesApi {
    uint32_t abi_version;
    // Transactional replacement for the next rendered frame. count=0 clears.
    // Call before draw(). Previous state advances ONLY after a successful draw.
    // Unsubmitted IDs disappear from history; no index-based fallbacks.
    int (*set_instances)(void* renderer,
                         const NorthStarRenderTemporalInstance*,uint32_t count);
} NorthStarRenderTemporalInstancesApi;
#ifdef __cplusplus
}
#endif
