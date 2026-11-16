// Optional engine.vehicle cabin and door actuator ABI v3.
// Synchronous POD C contract; ABI v1/v2 remain unchanged.
#pragma once
#include <northstarcpp/vehicle_dynamics_plugin.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_VEHICLE_CABIN_ABI 3u
#define NORTHSTARCPP_VEHICLE_MAX_DOORS 12u
typedef struct NorthStarVehicleDoorConfig {
    uint32_t size;
    float hinge[3];     // vehicle local metres, relative to body pivot
    float open_angle;   // positive/negative yaw radians
    float angular_speed;// radians / second
} NorthStarVehicleDoorConfig;
typedef struct NorthStarVehicleDoorState {
    uint32_t size;
    uint32_t index;
    float fraction;       // 0 closed, 1 fully open
    float target_fraction;// requested state
    float angle;          // open_angle * fraction, radians
} NorthStarVehicleDoorState;
typedef struct NorthStarVehicleCabinApi {
    uint32_t abi_version;
    uint32_t size;
    int (*configure_doors)(void* vehicle_world,uint32_t entity_id,
                           NorthStarVehicleDoorConfig const* doors,uint32_t count);
    int (*set_door)(void* vehicle_world,uint32_t entity_id,
                    uint32_t ordinal,uint32_t open);
    int (*door_state)(void* vehicle_world,uint32_t entity_id,
                      uint32_t ordinal,NorthStarVehicleDoorState* output);
    uint32_t (*door_count)(void* vehicle_world,uint32_t entity_id);
} NorthStarVehicleCabinApi;
#ifdef __cplusplus
}
#endif
