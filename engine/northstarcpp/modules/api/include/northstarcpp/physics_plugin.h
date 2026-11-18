// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once
#include <northstarcpp/module.h>

#define NORTHSTARCPP_PHYSICS_ABI 4u

typedef struct NorthStarRigidBody {
    float x, y, z; // bottom-center before rotation
    float size, mass;
    uint32_t initially_controlled; // physics ABI 4 compatibility, not SceneObject ownership
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

// Optional PlayerFramework/Jolt character possession extension.
// ABI 4 remains unchanged. Index UINT32_MAX unpossesses without destroying
// the controller or physics world. Values must be finite and validated.
#define NORTHSTARCPP_PHYSICS_CHARACTER_ABI 1u
#define NORTHSTARCPP_PHYSICS_CHARACTER_MODULE_ABI 0x50484301u
typedef struct NorthStarPhysicsCharacterMotor {
    float walk_speed;
    float run_speed;
    float jump_speed;
    float acceleration;
    float air_control;
    float gravity_scale;
} NorthStarPhysicsCharacterMotor;
typedef struct NorthStarPhysicsCharacterApi {
    uint32_t abi_version;
    int (*possess_body)(void* world,uint32_t index);
    int (*set_sprinting)(void* world,uint32_t sprinting);
    int (*configure_motor)(void* world,uint32_t index,
        NorthStarPhysicsCharacterMotor const* motor);
} NorthStarPhysicsCharacterApi;
// Optional collision-resolved animation displacement. The rigidbody motor
// consumes deltas as velocity during the next physics tick, allowing Jolt
// broadphase/narrowphase contacts to prevent penetration. Legacy ABI 4 and
// Character ABI 1 are unchanged. Unsupported providers fail explicitly.
// Optional per-NPC motor extension. No change to core Physics ABI 4.
// Each intent is expressed as planar normalized direction and speed (m/s),
// applied by Jolt inside the same fixed-step collision solver as the player.
// Clearing a command stops active planar driving, never teleports the NPC.
#define NORTHSTARCPP_PHYSICS_AI_MOTOR_ABI 1u
#define NORTHSTARCPP_PHYSICS_AI_MOTOR_MODULE_ABI 0x50484101u
typedef struct NorthStarPhysicsAiMotorApi {
    uint32_t abi_version;
    int (*set_intent)(void* world,uint32_t body_index,
                      float input_x,float input_z,float speed);
    int (*clear_intent)(void* world,uint32_t body_index);
} NorthStarPhysicsAiMotorApi;

#define NORTHSTARCPP_PHYSICS_ROOT_MOTION_ABI 1u
#define NORTHSTARCPP_PHYSICS_ROOT_MOTION_MODULE_ABI 0x50485208u
typedef struct NorthStarPhysicsRootMotionApi {
    uint32_t abi_version;
    // Per simulation frame, world-space meters. No direct pose teleport.
    // The provider must reject non-possessed or invalid targets.
    int (*submit)(void* world,uint32_t controlled_index,
        float dx,float dy,float dz,float seconds);
} NorthStarPhysicsRootMotionApi;

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


// Optional collision-shape authoring extension. Existing ABI 4 is unchanged.
#define NORTHSTARCPP_PHYSICS_COLLIDER_ABI 1u
#define NORTHSTARCPP_PHYSICS_COLLIDER_MODULE_ABI 0x50485203u
typedef struct NorthStarPhysicsCollider {
    uint32_t enabled;   // zero disables broadphase collision
    uint32_t shape;     // 0 box, 1 sphere, 2 capsule (Y axis)
    float width, height, depth; // full dimensions, meters
    float offset_x, offset_y, offset_z; // relative to centered collider
} NorthStarPhysicsCollider;
typedef struct NorthStarPhysicsColliderApi {
    uint32_t abi_version;
    int (*configure)(void* world, uint32_t index,
                     const NorthStarPhysicsCollider* collider);
} NorthStarPhysicsColliderApi;
typedef const NorthStarPhysicsColliderApi*
    (*NorthStarPhysicsGetColliderApi)(uint32_t requested_version);

// Optional advanced mesh collision and object-channel extension.
// Same fixed C ABI for dynamic convex and static complex triangle geometry.
// mesh vertices are transformed to body-local meters by the host.
// mode=3 exact trimesh (STATIC only), mode=4 auto-convex split hulls.
// splitsNum=1..16 bounds generated convex parts. Providers must copy data.
#define NORTHSTARCPP_PHYSICS_MESH_ABI 1u
#define NORTHSTARCPP_PHYSICS_MESH_MODULE_ABI 0x50485204u
typedef struct NorthStarPhysicsMeshApi {
    uint32_t abi_version;
    int (*configure_mesh)(void* world, uint32_t index,
                          uint32_t mode, uint32_t splits_num,
                          const float* vertices, uint32_t vertex_count,
                          const uint32_t* indices, uint32_t index_count);
    int (*set_object_type)(void* world, uint32_t index, uint32_t object_type);
    // Returns 0..5, UINT32_MAX for invalid body index.
    uint32_t (*object_type)(void* world, uint32_t index);
} NorthStarPhysicsMeshApi;
typedef const NorthStarPhysicsMeshApi*
    (*NorthStarPhysicsGetMeshApi)(uint32_t requested_version);


// Optional per-triangle physical surface extension. ABI 4 remains byte-for-byte
// unchanged; old providers continue reporting only object+normal.
// IDs: 0 unknown, 1 dirt, 2 grass, 3 metal, 4 mud, 5 snow,
//      6 stone, 7 water, 8 wood.
#define NORTHSTARCPP_PHYSICS_SURFACE_ABI 1u
#define NORTHSTARCPP_PHYSICS_SURFACE_MODULE_ABI 0x50485301u
typedef struct NorthStarPhysicsSurfaceHit {
    NorthStarPhysicsCastHit cast;
    uint32_t triangle_index; // original authoring triangle, UINT32_MAX if absent
    uint32_t material_id;    // 0 unknown, 1..8 physical surface category
} NorthStarPhysicsSurfaceHit;
typedef struct NorthStarPhysicsSurfaceApi {
    uint32_t abi_version;
    // Per-triangle IDs for an already configured static ComplexMesh actor.
    // Pointers are borrowed; provider copies the IDs. Count is triangle count.
    int (*set_triangle_materials)(void* world, uint32_t object_index,
                                  const uint32_t* material_ids, uint32_t count);
    // Adds a standalone scene mesh with its per-triangle material IDs.
    int (*add_static_mesh_materials)(void* world,
        const float* xyz, uint32_t vertex_count,
        const uint32_t* indices, uint32_t index_count,
        const uint32_t* material_ids, uint32_t material_count);
    // Equivalent to ABI-4 sweep, with actual hit triangle and physical material.
    int (*sweep_sphere_surface)(void* world,
        float sx,float sy,float sz,float ex,float ey,float ez,
        float radius,uint32_t ignore_index,NorthStarPhysicsSurfaceHit* hit);
} NorthStarPhysicsSurfaceApi;
typedef const NorthStarPhysicsSurfaceApi*
    (*NorthStarPhysicsGetSurfaceApi)(uint32_t version);

// Optional precomputed V-HACD data: no voxelization during gameplay.
// Each hull is already convex, in the body's local coordinate system.
#define NORTHSTARCPP_PHYSICS_COOKED_HULLS_ABI 1u
#define NORTHSTARCPP_PHYSICS_COOKED_HULLS_MODULE_ABI 0x50485205u
typedef struct NorthStarPhysicsCookedHull {
    const float* xyz;
    uint32_t vertex_count;
    const uint32_t* indices;
    uint32_t index_count;
} NorthStarPhysicsCookedHull;
typedef struct NorthStarPhysicsCookedHullsApi {
    uint32_t abi_version;
    int (*configure_hulls)(void* world, uint32_t index,
        const NorthStarPhysicsCookedHull* hulls, uint32_t hull_count);
} NorthStarPhysicsCookedHullsApi;
// UE-style six-channel collision response matrix.
// 0=Ignore (no broadphase pair), 1=Overlap (sensor, no impulse), 2=Block.
// Channels correspond to WorldStatic, WorldDynamic, Pawn, PhysicsBody,
// Vehicle and Destructible. The provider also reports overlap events.
#define NORTHSTARCPP_PHYSICS_RESPONSE_ABI 1u
#define NORTHSTARCPP_PHYSICS_RESPONSE_MODULE_ABI 0x50485206u
typedef struct NorthStarPhysicsContactEvent {
    uint32_t first_index;
    uint32_t second_index; // UINT32_MAX = ground/unnamed geometry
    uint32_t phase;        // 0=Begin, 1=End
    uint32_t response;     // 1=Overlap, 2=Block
} NorthStarPhysicsContactEvent;
typedef struct NorthStarPhysicsResponseApi {
    uint32_t abi_version;
    int (*configure)(void* world,uint32_t index,uint32_t object_type,
                     const uint8_t responses[6]);
    int (*query)(void* world,uint32_t index,uint32_t* object_type,
                 uint8_t responses[6]);
    uint32_t (*drain_events)(void* world,NorthStarPhysicsContactEvent* out,
                             uint32_t capacity);
} NorthStarPhysicsResponseApi;
// Optional extended 16-channel collision profiles and filtered queries.
// response_mask bits select Overlap (2) or Block (4).
#define NORTHSTARCPP_PHYSICS_CHANNELS_ABI 1u
#define NORTHSTARCPP_PHYSICS_CHANNELS_MODULE_ABI 0x50485207u
typedef struct NorthStarPhysicsChannelsApi {
    uint32_t abi_version;
    int (*configure)(void*,uint32_t,uint32_t,const uint8_t*,uint32_t);
    int (*query)(void*,uint32_t,uint32_t*,uint8_t*,uint32_t);
    int (*sweep_sphere)(void*,float,float,float,float,float,float,float,
                        uint32_t,uint32_t,uint32_t,NorthStarPhysicsCastHit*);
    int (*raycast)(void*,float,float,float,float,float,float,
                   uint32_t,uint32_t,uint32_t,NorthStarPhysicsCastHit*);
} NorthStarPhysicsChannelsApi;
// Optional runtime rigid-body lifecycle extension. Stable, append-only body
// indices; removal leaves a tombstone until the world is destroyed. Legacy
// NORTHSTARCPP_PHYSICS_ABI 4 function tables remain unchanged.
// Optional material response settings for any dynamic physics body.
// Runtime clients do not need a new ABI-4 rigid-body layout.
// restitution=0..1, friction=0..1; providers may decline if unsupported.
#define NORTHSTARCPP_PHYSICS_BODY_MATERIAL_ABI 1u
#define NORTHSTARCPP_PHYSICS_BODY_MATERIAL_MODULE_ABI 0x50484D01u
typedef struct NorthStarPhysicsBodyMaterialApi {
    uint32_t abi_version;
    int (*configure)(void* world,uint32_t index,float friction,float restitution);
} NorthStarPhysicsBodyMaterialApi;

#define NORTHSTARCPP_PHYSICS_RUNTIME_ABI 1u
#define NORTHSTARCPP_PHYSICS_RUNTIME_MODULE_ABI 0x50485202u
typedef struct NorthStarPhysicsRuntimeApi {
    uint32_t abi_version;
    int (*add_body)(void* world, const NorthStarRigidBody* body, uint32_t* index);
    int (*remove_body)(void* world, uint32_t index);
} NorthStarPhysicsRuntimeApi;
typedef const NorthStarPhysicsRuntimeApi*
    (*NorthStarPhysicsGetRuntimeApi)(uint32_t requested_version);
