// Host-owned engine.collision service; no private C++ layouts cross the ABI.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/collision_plugin.h>
#include <northstarcpp/math.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace northstarcpp {
struct CollisionMeshHit {
    float distance=0.f;
    std::uint32_t triangle=0;
};
class CollisionService final {
public:
    CollisionService();
    ~CollisionService();
    CollisionService(CollisionService const&)=delete;
    CollisionService& operator=(CollisionService const&)=delete;
    std::string provider_name() const;
    std::optional<float> ray_box(math::float3 origin,math::float3 direction,
                                  math::float3 low,math::float3 high,
                                  float max_distance) const;
    std::optional<CollisionMeshHit> ray_mesh(
        math::float3 origin,math::float3 direction,
        float const* xyz,std::uint32_t vertex_count,
        std::uint16_t const* indices,std::uint32_t index_count,
        float max_distance) const;
    std::optional<CollisionMeshHit> ray_mesh(
        math::float3 origin,math::float3 direction,
        std::vector<float> const& xyz,std::vector<std::uint16_t> const& indices,
        float max_distance) const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace northstarcpp
