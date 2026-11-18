// NorthStarCpp optional engine.vehicle powertrain ABI v4.
// Original implementation; preserves the existing vehicle ABIs v1/v2/v3.
// Units: RPM, newton-metres, seconds, metres, ratios (unitless).
#pragma once
#include <northstarcpp/vehicle_cabin_plugin.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_VEHICLE_POWERTRAIN_ABI 4u
#define NORTHSTARCPP_VEHICLE_MAX_FORWARD_GEARS 8u

typedef struct NorthStarVehiclePowertrainConfig {
    uint32_t size;
    uint32_t forward_gear_count; // 1..8
    uint32_t automatic;          // 0 manual, 1 automatic
    float ratios[NORTHSTARCPP_VEHICLE_MAX_FORWARD_GEARS]; // positive, descending
    float reverse_ratio;         // magnitude, >0
    float final_drive;           // >0
    float efficiency;            // fraction 0..1
    float peak_torque;           // engine Nm
    float idle_rpm;
    float redline_rpm;
    float shift_up_rpm;
    float shift_down_rpm;
    float shift_seconds;
} NorthStarVehiclePowertrainConfig;

typedef struct NorthStarVehiclePowertrainState {
    uint32_t size;
    int32_t gear;                // -1 reverse, 0 neutral, 1..forward_gear_count
    float engine_rpm;
    float delivered_torque_nm;  // engine torque before gear reduction, 0 while shifting
    float wheel_force_n;        // signed total drive force at contact patch
    float shift_remaining_s;
} NorthStarVehiclePowertrainState;

typedef struct NorthStarVehiclePowertrainApi {
    uint32_t abi_version;
    uint32_t struct_size;
    int (*configure)(void* world,uint32_t entity_id,
                     NorthStarVehiclePowertrainConfig const*);
    int (*state)(void* world,uint32_t entity_id,
                 NorthStarVehiclePowertrainState*);
    int (*select_gear)(void* world,uint32_t entity_id,int32_t gear);
} NorthStarVehiclePowertrainApi;
#ifdef __cplusplus
}
#endif
