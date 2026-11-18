// NorthStarCpp optional indexed rigid-mesh GPU instances ABI v1.
// CPU source pixels/vertices/indices are immutable during upload_mesh().
// Material upload reuses the existing UV/PBR slot contract. External
// providers can omit this extension and receive full-quality legacy draws.
#pragma once
#include <northstarcpp/render_materials_api.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_RENDER_RIGID_INSTANCES_ABI 1u
#define NORTHSTARCPP_RENDER_RIGID_INSTANCES_MODULE_ABI 0x52474901u
#define NORTHSTARCPP_RIGID_MAX_MESH_VERTICES 2000000u
#define NORTHSTARCPP_RIGID_MAX_MESH_INDICES 9000000u
typedef struct NorthStarRigidMesh {
    uint64_t key; // stable within a project mount; invalidated by clear_meshes
    const NorthStarMaterialVertex* vertices; // local-space original vertices
    uint32_t vertex_count;
    const uint32_t* indices; // original triangle-list indices
    uint32_t index_count;
} NorthStarRigidMesh;
typedef struct NorthStarRigidInstance {
    uint64_t mesh_key;
    uint64_t entity_id;
    uint32_t first_index; // original submesh material section range
    uint32_t index_count;
    // Row-major 4x4 world transform, row-vector multiplication.
    // CPU submits only this + PBR material data each frame.
    float world[16];
    NorthStarMaterialBatch material; // material.vertices and vertex_count unused
} NorthStarRigidInstance;
typedef struct NorthStarRigidInstanceStats {
    uint32_t struct_size;
    uint32_t active_draws;
    uint32_t resident_meshes;
    uint32_t reserved;
    uint64_t resident_geometry_bytes;
    uint64_t lifetime_geometry_uploaded_bytes;
    uint64_t last_transform_bytes; // 64 bytes per active world matrix
    uint64_t last_material_constant_bytes; // full rigid CB including transform
    uint64_t last_drawn_indices;
} NorthStarRigidInstanceStats;
typedef struct NorthStarRenderRigidInstancesApi {
    uint32_t abi_version;
    uint32_t struct_size;
    // Copies all vertices and indices into GPU-owned immutable resources.
    // Idempotent by key until clear_meshes. No hidden reduction/decimation.
    int (*upload_mesh)(void*,const NorthStarRigidMesh*);
    int (*clear_meshes)(void*);
    // Replaces the entire next-frame opaque instance list. count=0 clears.
    int (*set_instances)(void*,const NorthStarRigidInstance*,uint32_t count);
    int (*read_stats)(void*,NorthStarRigidInstanceStats*);
} NorthStarRenderRigidInstancesApi;
#ifdef __cplusplus
}
#endif
