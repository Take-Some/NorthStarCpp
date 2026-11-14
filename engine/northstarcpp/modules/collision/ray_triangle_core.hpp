// NorthStarCpp collision ray/triangle kernel.
// Adapted from the engine's collision/sources/colliders_ray.cpp implementation,
// without the historic Opcode cell and pch dependencies.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/math.hpp>
#include <optional>
namespace northstarcpp::collision {
// Input direction must be nonzero; t is a ray parameter, not a normalized
// distance unless the direction has unit length.
std::optional<float> ray_triangle_entry(
    math::float3 const& first,math::float3 const& second,
    math::float3 const& third,math::float3 const& origin,
    math::float3 const& direction,float maximum_parameter) noexcept;
} // namespace northstarcpp::collision
