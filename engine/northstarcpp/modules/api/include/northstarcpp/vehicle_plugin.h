// NorthStarCpp native engine.vehicle provider ABI v1.
// Independent C ABI; each world belongs to one running scene/session.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_VEHICLE_ABI 1u
typedef struct NorthStarVehicleDefinition {
    uint32_t size;
    uint32_t entity_index;
    float position[3];       // metres, world space
    float heading;           // radians, 0 = +Z, positive = +X
    float wheelbase;         // metres
    float maximum_speed;     // metres/second
    float acceleration;      // metres/second^2
    float braking;           // metres/second^2
    float maximum_steering;  // radians
    float rolling_drag;      // 1/seconds
} NorthStarVehicleDefinition;
typedef struct NorthStarVehicleState {
    uint32_t size;
    uint32_t entity_index;
    float position[3];
    float heading;           // radians
    float speed;             // metres/second, negative when reversing
    float steering;          // input -1..1
    float throttle;          // input -1..1
    float brake;             // input 0..1
    uint32_t waypoint_active;
} NorthStarVehicleState;
typedef struct NorthStarVehicleApi {
    void* (*create_world)(void);
    void (*destroy_world)(void*);
    int (*attach)(void*, NorthStarVehicleDefinition const*);
    int (*detach)(void*, uint32_t entity_index);
    int (*controls)(void*, uint32_t entity_index,
                    float throttle, float steering, float brake);
    int (*waypoint)(void*, uint32_t entity_index, float x, float z,
                    float requested_speed, float arrival_radius);
    int (*step)(void*, float dt);
    int (*read)(void*, uint32_t entity_index, NorthStarVehicleState*);
    int (*at)(void*, uint32_t ordinal, NorthStarVehicleState*);
    uint32_t (*count)(void*);
} NorthStarVehicleApi;
#ifdef __cplusplus
}
#endif
