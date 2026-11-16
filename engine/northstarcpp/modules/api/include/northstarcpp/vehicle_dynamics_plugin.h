// NorthStar C++ optional engine.vehicle Dynamics ABI v2.
// ABI v1 remains intact for external providers and existing game scripts.
// Units: metres, seconds, kilograms, newtons, radians. Y is up.
#pragma once
#include <northstarcpp/vehicle_plugin.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_VEHICLE_DYNAMICS_ABI 2u
#define NORTHSTARCPP_VEHICLE_MAX_WHEELS 16u
#define NORTHSTARCPP_VEHICLE_MAX_SEATS 64u

typedef struct NorthStarVehicleWheelConfig {
    uint32_t size;
    float mount[3];        // local X/Y/Z relative to vehicle reference point
    float radius;          // wheel radius, m
    float rest_length;     // suspension extension, m
    float travel;          // suspension stroke, m
    float spring_rate;     // N/m
    float damper_rate;     // N*s/m
    float grip_long;       // scale applied to surface Coulomb mu
    float grip_lat;        // scale applied to surface Coulomb mu
    float steer_factor;    // front steering: 0..1
    float drive_factor;    // shares available drive torque, 0..1
    float brake_factor;    // shares friction braking, 0..1
    float rotational_inertia; // kg*m^2
} NorthStarVehicleWheelConfig;

typedef struct NorthStarVehicleSeatConfig {
    uint32_t size;
    float position[3];     // local X/Y/Z
    float yaw;             // local facing relative to vehicle heading
    uint32_t role;         // 0 passenger, 1 driver
} NorthStarVehicleSeatConfig;

typedef struct NorthStarVehicleRig {
    uint32_t size;
    float mass;            // kg
    float yaw_inertia;     // kg*m^2
    float center_of_mass_y; // metres above axle plane
    float engine_force;    // max drive force at tyre contact, newtons
    float brake_force;     // max braking force, newtons
    float aerodynamic_drag; // N/(m/s)^2
    float rolling_resistance; // fraction of normal force
    uint32_t abs_enabled;
    uint32_t traction_control_enabled;
    uint32_t wheel_count;
    NorthStarVehicleWheelConfig const* wheels; // copied synchronously
    uint32_t seat_count;
    NorthStarVehicleSeatConfig const* seats;   // copied synchronously
} NorthStarVehicleRig;

typedef struct NorthStarVehicleWheelState {
    uint32_t size;
    float steer_angle;       // radians
    float spin_angle;        // radians
    float angular_speed;     // rad/s
    float compression;       // metres
    float normal_force;      // newtons
    float slip_ratio;        // dimensionless
    float slip_angle;        // radians
    uint32_t in_contact;
} NorthStarVehicleWheelState;

typedef struct NorthStarVehicleSeatState {
    uint32_t size;
    uint32_t index;
    uint32_t role;
    uint64_t occupant;       // zero means unoccupied
    float world_position[3];
    float world_yaw;
} NorthStarVehicleSeatState;

typedef struct NorthStarVehicleMotionState {
    uint32_t size;
    float forward_speed;     // m/s
    float lateral_speed;     // m/s
    float yaw_rate;          // rad/s
    float longitudinal_slip; // max wheel slip ratio
    float lateral_slip;      // signed tyre slip angle proxy, radians
    float travelled_distance;// accumulated metres
    uint32_t grounded_wheels;
} NorthStarVehicleMotionState;

typedef struct NorthStarVehicleGroundContact {
    uint32_t size;
    uint32_t wheel_index;
    uint32_t in_contact;     // 0 airborne, 1 grounded
    float ground_y;         // world-space road surface height at wheel
    float friction;         // surface mu: 0.05..3
} NorthStarVehicleGroundContact;

typedef struct NorthStarVehicleCollision {
    uint32_t size;
    float position[3];      // non-penetrating corrected world-space position
    float normal_x;
    float normal_z;         // horizontal impact normal pointing away from obstacle
    float restitution;      // 0..0.5
} NorthStarVehicleCollision;

// Same capability, different module API version queried via northstar_module_query.
// All tables are immutable; their functions do not leak C++ types across DLLs.
typedef struct NorthStarVehicleDynamicsApi {
    uint32_t abi_version;
    uint32_t struct_size;
    int (*configure)(void* world,uint32_t id,NorthStarVehicleRig const* rig);
    uint32_t (*wheel_count)(void* world,uint32_t id);
    int (*wheel_state)(void* world,uint32_t id,uint32_t ordinal,NorthStarVehicleWheelState*);
    uint32_t (*seat_count)(void* world,uint32_t id);
    int (*seat_state)(void* world,uint32_t id,uint32_t ordinal,NorthStarVehicleSeatState*);
    int (*occupy_seat)(void* world,uint32_t id,uint32_t ordinal,uint64_t occupant);
    int (*release_seat)(void* world,uint32_t id,uint32_t ordinal,uint64_t occupant);
    int (*ground_contact)(void* world,uint32_t id,NorthStarVehicleGroundContact const*);
    int (*motion_state)(void* world,uint32_t id,NorthStarVehicleMotionState*);
    int (*resolve_collision)(void* world,uint32_t id,NorthStarVehicleCollision const*);
} NorthStarVehicleDynamicsApi;
#ifdef __cplusplus
}
#endif
