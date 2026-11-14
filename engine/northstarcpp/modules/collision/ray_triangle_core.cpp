// Native NorthStarCpp ray-triangle kernel, originally used by the collision
// BVH; now independently compiled and shared by engine.collision providers.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include "ray_triangle_core.hpp"
#include <cmath>
namespace northstarcpp::collision {
std::optional<float> ray_triangle_entry(
    math::float3 const& v0,math::float3 const& v1,math::float3 const& v2,
    math::float3 const& position,math::float3 const& direction,
    float const maximum_parameter) noexcept {
    // The same Moller-Trumbore non-culling test used by colliders::ray,
    // with explicit finite/range checks at the modern provider boundary.
    math::float3 const edge1=v1-v0;
    math::float3 const edge2=v2-v0;
    math::float3 normal=direction^edge2;
    float const determinant=edge1|normal;
    if(!std::isfinite(determinant) ||
       math::is_zero(determinant,math::epsilon_5))return {};
    math::float3 const temp=position-v0;
    float const inverted=1.f/determinant;
    float const u=temp.dot_product(normal)*inverted;
    if(!(u>=0.f && u<=1.f))return {};
    normal=temp^edge1;
    float const v=direction.dot_product(normal)*inverted;
    if(!(v>=0.f && u+v<=1.f))return {};
    float const t=(edge2|normal)*inverted;
    if(!std::isfinite(t) || t<=0.f || t>maximum_parameter)return {};
    return t;
}
} // namespace northstarcpp::collision
