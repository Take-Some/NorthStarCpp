// engine.collision host bridge; provider selection honors builtin/external ABI.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/collision_service.hpp>
#include "module_loader.hpp"
#include <limits>
#include <stdexcept>
namespace northstarcpp {
struct CollisionService::State {
    Module module{NORTHSTARCPP_CAPABILITY_COLLISION};
    NorthStarCollisionApi const& api=
        *module.query<NorthStarCollisionApi>(NORTHSTARCPP_COLLISION_ABI);
};
CollisionService::CollisionService():state_(std::make_unique<State>()){}
CollisionService::~CollisionService()=default;
std::string CollisionService::provider_name() const {
    return state_->module.provider();
}
static NorthStarCollisionRay ray_data(math::float3 origin,
                                      math::float3 direction,float distance) {
    return {sizeof(NorthStarCollisionRay),
            {origin.x,origin.y,origin.z},
            {direction.x,direction.y,direction.z},distance};
}
std::optional<float> CollisionService::ray_box(
    math::float3 origin,math::float3 direction,
    math::float3 low,math::float3 high,float maximum) const {
    auto ray=ray_data(origin,direction,maximum);
    NorthStarCollisionBounds bounds{sizeof(NorthStarCollisionBounds),
        {low.x,low.y,low.z},{high.x,high.y,high.z}};
    NorthStarCollisionHit hit{sizeof(NorthStarCollisionHit),0,0.f,0};
    if(!state_->api.ray_aabb(&ray,&bounds,&hit))
        throw state_->module.error("engine.collision ray/AABB failed");
    if(hit.size!=sizeof(hit) || hit.hit>1 ||
       (hit.hit && (hit.distance<0.f || hit.distance>maximum)))
        throw std::runtime_error("Invalid collision provider AABB result");
    return hit.hit?std::optional<float>(hit.distance):std::nullopt;
}
std::optional<CollisionMeshHit> CollisionService::ray_mesh(
    math::float3 origin,math::float3 direction,float const* xyz,
    std::uint32_t vertex_count,std::uint16_t const* indices,
    std::uint32_t index_count,float maximum) const {
    auto ray=ray_data(origin,direction,maximum);
    NorthStarCollisionHit hit{sizeof(NorthStarCollisionHit),0,0.f,0};
    if(!state_->api.ray_triangles(&ray,xyz,vertex_count,indices,index_count,&hit))
        throw state_->module.error("engine.collision triangle raycast failed");
    if(hit.size!=sizeof(hit) || hit.hit>1 ||
       (hit.hit && (hit.distance<=0.f || hit.distance>maximum ||
                     hit.triangle>=index_count/3u)))
        throw std::runtime_error("Invalid collision provider triangle result");
    return hit.hit?std::optional<CollisionMeshHit>(
        CollisionMeshHit{hit.distance,hit.triangle}):std::nullopt;
}
std::optional<CollisionMeshHit> CollisionService::ray_mesh(
    math::float3 origin,math::float3 direction,
    std::vector<float> const& xyz,std::vector<std::uint16_t> const& indices,
    float maximum) const {
    if(xyz.size()%3 || xyz.size()/3>std::numeric_limits<std::uint32_t>::max() ||
       indices.size()>std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Invalid geometry buffer sizes");
    return ray_mesh(origin,direction,xyz.data(),
        static_cast<std::uint32_t>(xyz.size()/3),indices.data(),
        static_cast<std::uint32_t>(indices.size()),maximum);
}
} // namespace northstarcpp
