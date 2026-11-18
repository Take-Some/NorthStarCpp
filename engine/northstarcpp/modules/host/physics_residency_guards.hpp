// NorthStarCpp: inexpensive, reusable per-vehicle streaming guards.
#pragma once

#include <northstarcpp/physics_vehicle_plugin.h>

#include <algorithm>
#include <cmath>

namespace northstarcpp::physics_detail {

// Precompute the two-second lookahead and speed-derived protection radius
// once per vehicle per streaming pass, never once per (object, vehicle) pair.
struct VehicleResidencyGuard {
    float predicted_x = 0.f;
    float predicted_y = 0.f;
    float predicted_z = 0.f;
    float base_radius = 0.f;

    static VehicleResidencyGuard from_pose(
        NorthStarPhysicsVehiclePose const& pose) noexcept {
        return {
            pose.x + pose.linear_velocity[0] * 2.f,
            pose.y,
            pose.z + pose.linear_velocity[2] * 2.f,
            std::clamp(
                48.f + 2.f * std::hypot(
                    pose.linear_velocity[0], pose.linear_velocity[2]),
                48.f, 180.f)
        };
    }

    bool contains(float x, float y, float z, float object_size) const noexcept {
        float const radius = base_radius + object_size;
        float const dx = x - predicted_x;
        float const dy = y - predicted_y;
        float const dz = z - predicted_z;
        return dx * dx + dy * dy + dz * dz <= radius * radius;
    }
};

} // namespace northstarcpp::physics_detail
