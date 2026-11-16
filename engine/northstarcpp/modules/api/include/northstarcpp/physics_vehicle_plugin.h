// Optional native 6-DOF vehicle dynamics extension for engine.physics.
// Provider-owned Jolt VehicleConstraint; all settings are synchronous POD copies.
// No changes to existing NorthStarPhysicsApi ABI 4.
#pragma once
#include <northstarcpp/vehicle_dynamics_plugin.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_PHYSICS_VEHICLE_ABI 1u
#define NORTHSTARCPP_PHYSICS_VEHICLE_MODULE_ABI 0x50485601u

typedef struct NorthStarPhysicsVehiclePose {
    uint32_t size;
    float x,y,z;              // same bottom-center coordinate as SceneObject
    float rotation[9];        // columns are local basis vectors world-space
    float linear_velocity[3]; // world m/s
    float angular_velocity[3];// world rad/s
} NorthStarPhysicsVehiclePose;

typedef struct NorthStarPhysicsVehicleApi {
    uint32_t abi_version;
    uint32_t size;
    // Replaces one authored static body with dynamic rigid chassis, preserving
    // stable entity index and authored collision shape.
    int (*create)(void* physics_world,uint32_t object_index,
                  const NorthStarVehicleRig* rig,float heading_rad);
    int (*destroy)(void* physics_world,uint32_t object_index);
    int (*controls)(void* physics_world,uint32_t object_index,
                    float throttle,float steer,float brake,float handbrake);
    int (*pose)(void* physics_world,uint32_t object_index,
                NorthStarPhysicsVehiclePose* out);
    int (*wheel)(void* physics_world,uint32_t object_index,
                 uint32_t wheel_index,NorthStarVehicleWheelState* out);
    uint32_t (*wheel_count)(void* physics_world,uint32_t object_index);
} NorthStarPhysicsVehicleApi;

#ifdef __cplusplus
}
#endif
