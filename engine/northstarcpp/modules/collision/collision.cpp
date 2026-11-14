// NorthStarCpp engine.collision provider built on foundation collision kernels.
// Both builtin and independently loaded DLL instances expose the same C ABI.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/collision_plugin.h>
#include <northstarcpp/collision_queries.hpp>
#include "ray_triangle_core.hpp"
#include "module_provider.hpp"
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace {
using northstarcpp::module_provider::guarded;
void require(bool valid,char const* reason) {
    if(!valid)throw std::invalid_argument(reason);
}
northstarcpp::math::float3 point(float const (&values)[3]) {
    return {values[0],values[1],values[2]};
}
void validate_ray(NorthStarCollisionRay const* ray) {
    require(ray && ray->size>=sizeof(NorthStarCollisionRay),
            "Missing or invalid collision ray");
    require(ray->maximum_distance>=0.f && std::isfinite(ray->maximum_distance),
            "Invalid collision range");
    float squared=0.f;
    for(int i=0;i<3;++i) {
        require(std::isfinite(ray->origin[i]) &&
                std::isfinite(ray->direction[i]),"Non-finite ray");
        squared+=ray->direction[i]*ray->direction[i];
    }
    require(squared>0.f && std::isfinite(squared),"Degenerate ray direction");
}
void validate_hit(NorthStarCollisionHit const* hit) {
    require(hit && hit->size>=sizeof(NorthStarCollisionHit),
            "Missing collision output");
}
int ray_aabb(NorthStarCollisionRay const* ray,
             NorthStarCollisionBounds const* bounds,
             NorthStarCollisionHit* hit) {
    return guarded([&]{
        validate_ray(ray);
        validate_hit(hit);
        require(bounds && bounds->size>=sizeof(NorthStarCollisionBounds),
                "Invalid collision bounds");
        for(int i=0;i<3;++i)
            require(std::isfinite(bounds->minimum[i]) &&
                    std::isfinite(bounds->maximum[i]) &&
                    bounds->minimum[i]<=bounds->maximum[i],
                    "Invalid AABB coordinates");
        northstarcpp::math::aabb area;
        area.min=point(bounds->minimum);
        area.max=point(bounds->maximum);
        auto const found=northstarcpp::collision::ray_box_entry(
            area,point(ray->origin),point(ray->direction),ray->maximum_distance);
        NorthStarCollisionHit result{
            sizeof(NorthStarCollisionHit),found?1u:0u,found.value_or(0.f),0u};
        *hit=result;
    });
}
int ray_triangles(NorthStarCollisionRay const* ray,
                  float const* xyz,std::uint32_t vertex_count,
                  std::uint16_t const* indices,std::uint32_t index_count,
                  NorthStarCollisionHit* hit) {
    return guarded([&]{
        validate_ray(ray);
        validate_hit(hit);
        require(xyz && indices && vertex_count>=3 &&
                vertex_count<=65536 && index_count>=3 &&
                index_count<=393216 && index_count%3==0,
                "Invalid collision triangle buffer");
        // Validate the complete input *before* writing a hit or touching
        // arbitrary vertex offsets supplied by external provider clients.
        for(std::uint32_t i=0;i<vertex_count*3u;++i)
            require(std::isfinite(xyz[i]),"Non-finite triangle coordinate");
        for(std::uint32_t i=0;i<index_count;++i)
            require(indices[i]<vertex_count,"Triangle vertex index outside buffer");
        auto vertex=[&](std::uint32_t index) {
            auto const j=3u*index;
            return northstarcpp::math::float3{xyz[j],xyz[j+1u],xyz[j+2u]};
        };
        auto const origin=point(ray->origin);
        auto const direction=point(ray->direction);
        float nearest=ray->maximum_distance;
        bool found=false;
        std::uint32_t triangle_index=0;
        for(std::uint32_t i=0;i<index_count;i+=3u) {
            auto const distance=northstarcpp::collision::ray_triangle_entry(
                vertex(indices[i]),vertex(indices[i+1u]),vertex(indices[i+2u]),
                origin,direction,nearest);
            if(distance && (!found || *distance<nearest)) {
                found=true;nearest=*distance;triangle_index=i/3u;
            }
        }
        *hit={sizeof(NorthStarCollisionHit),found?1u:0u,
              found?nearest:0.f,found?triangle_index:0u};
    });
}
NorthStarCollisionApi const api{ray_aabb,ray_triangles};
NorthStarModule const module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_COLLISION,
    "collision","1.0.0",
    NORTHSTARCPP_COLLISION_ABI,sizeof(api),&api,
    northstarcpp::module_provider::last_error};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT NorthStarModule const* northstar_module_query(
    std::uint32_t host,char const* capability,std::uint32_t version) {
    return northstarcpp::module_provider::match(module,host,capability,version);
}
