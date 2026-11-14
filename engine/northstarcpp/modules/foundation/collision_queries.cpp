// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/collision_queries.hpp>

#include <algorithm>
#include <cmath>

namespace northstarcpp::collision {

// Scalar GCC port of the slab test in compat/collision/sources/colliders_ray_base_win.cpp.
std::optional<float> ray_box_entry(math::aabb const& bounds, math::float3 const& origin,
    math::float3 const& direction, float max_parameter) noexcept
{
    if (!std::isfinite(max_parameter) || max_parameter < 0.f ||
        (direction.x == 0.f && direction.y == 0.f && direction.z == 0.f))
        return std::nullopt;

    double entry = 0.0;
    double exit = max_parameter;
    for (unsigned axis = 0; axis < 3; ++axis) {
        double const low = bounds.min[axis];
        double const high = bounds.max[axis];
        double const position = origin[axis];
        double const velocity = direction[axis];
        if (!std::isfinite(low) || !std::isfinite(high) || low > high ||
            !std::isfinite(position) || !std::isfinite(velocity))
            return std::nullopt;
        if (velocity == 0.0) {
            if (position < low || position > high)
                return std::nullopt;
            continue;
        }
        double const near_plane = (low - position) / velocity;
        double const far_plane = (high - position) / velocity;
        entry = std::max(entry, std::min(near_plane, far_plane));
        exit = std::min(exit, std::max(near_plane, far_plane));
        if (entry > exit)
            return std::nullopt;
    }
    return static_cast<float>(entry);
}

} // namespace northstarcpp::collision
