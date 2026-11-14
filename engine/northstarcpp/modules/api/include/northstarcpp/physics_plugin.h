// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once
#include <northstarcpp/module.h>

#define NORTHSTARCPP_PHYSICS_ABI 4u

typedef struct NorthStarRigidBody {
    float x, y, z; // bottom-center before rotation
    float size, mass;
    uint32_t player;
} NorthStarRigidBody;

typedef struct NorthStarBodyPose {
    float x, y, z; // body center minus half the original height on Y
    float rotation[9]; // three world-space basis vectors (local X, Y, Z)
} NorthStarBodyPose;

// A provider-owned query result. Geometry can be scene boxes or added static
// triangle meshes. The ignored index refers to a scene-object body.
typedef struct NorthStarPhysicsCastHit {
    uint32_t hit;           // zero for miss
    uint32_t object_index;  // UINT32_MAX for terrain/unnamed mesh
    float fraction;         // [0,1] fraction of start->end
    float normal_x, normal_y, normal_z;
} NorthStarPhysicsCastHit;

typedef struct NorthStarPhysicsApi {
    void* (*create)(const NorthStarRigidBody* bodies, uint32_t count);
    void (*destroy)(void* world);
    int (*step)(void* world, float seconds, float input_x, float input_z);
    int (*read_poses)(void* world, NorthStarBodyPose* poses, uint32_t count);
    // Grounded player-only jump; returns 1 on a valid call, including no-op in air.
    int (*jump)(void* world);
    // Move one static collision body to an authored bottom-center position.
    int (*move_static)(void* world, uint32_t index, float x, float y, float z);
    // Sphere/capsule-like camera sweep against the provider's actual collision
    // shapes, including triangle meshes registered through add_static_mesh.
    int (*sweep_sphere)(void* world,
        float sx,float sy,float sz,float ex,float ey,float ez,
        float radius,uint32_t ignore_index,NorthStarPhysicsCastHit* result);
    // Copies vertices/indices: caller retains ownership, triangles are static.
    // Vertex XYZ floats, triangle indices grouped in triples.
    int (*add_static_mesh)(void* world,const float* vertices,uint32_t vertex_count,
                           const uint32_t* indices,uint32_t index_count);
    // Apply a world-space impulse to a non-player dynamic rigid body.
    // Reject static objects, the player, invalid IDs and non-finite impulses.
    int (*apply_impulse)(void* world, uint32_t index, float x, float y, float z);
} NorthStarPhysicsApi;

// Optional physics residency extension, independently versioned.
// The original NorthStarPhysicsApi ABI 4 remains unchanged, so existing
// providers continue to work (without sector-level collision eviction).
// A provider may export northstar_physics_get_residency_api(1).
#define NORTHSTARCPP_PHYSICS_RESIDENCY_ABI 1u
#define NORTHSTARCPP_PHYSICS_RESIDENCY_MODULE_ABI 0x50485201u
typedef struct NorthStarPhysicsResidencyApi {
    uint32_t abi_version;
    // Set collision participation for an authored STATIC body by its original
    // scene index; preserves its shape, ID and transform for later restoration.
    // Dynamic bodies and the player are not eligible; success returns 1.
    int (*set_static_resident)(void* world,uint32_t index,uint32_t resident);
    // Returns 1 for resident, 0 for suspended, -1 for invalid/nonstatic.
    int (*is_static_resident)(void* world,uint32_t index);
} NorthStarPhysicsResidencyApi;
typedef const NorthStarPhysicsResidencyApi*
    (*NorthStarPhysicsGetResidencyApi)(uint32_t requested_version);
