// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once

#include <optional>

#include <northstarcpp/math.hpp>

namespace northstarcpp::collision {

// Intersects origin + direction * t with bounds for t in [0, max_parameter].
// With a unit direction, the returned parameter is a world-space distance.
// An origin inside the box returns zero. Invalid inputs and misses return nullopt.
std::optional<float> ray_box_entry(math::aabb const& bounds, math::float3 const& origin,
    math::float3 const& direction, float max_parameter) noexcept;

} // namespace northstarcpp::collision
