// NorthStar C++ authored collision-shape metadata for PhysicsScene.
// Binary scene component payload is an exact C ABI NorthStarPhysicsCollider.
// Invalid authored values fail initialization rather than silently replacing shape.
#pragma once
#include <northstarcpp/scene.hpp>
#include <northstarcpp/physics_plugin.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace northstarcpp::collision_authoring {
struct Settings {
    std::uint32_t shape=0;
    Vector3 dimensions{1.f,1.f,1.f};
    Vector3 offset{0.f,0.f,0.f};
};
inline Settings defaults(float extent) {
    float const dimension=std::max(0.01f,extent);
    return {0,{dimension,dimension,dimension},{0.f,0.f,0.f}};
}
inline std::optional<Settings> lookup(Scene const& scene,std::uint64_t id) {
    std::optional<Settings> result;
    for(auto const& item:scene.components) {
        if(item.entity_id!=id || item.type!="engine.physics.collider")continue;
        if(result || item.version!=1 || item.payload.size()!=sizeof(NorthStarPhysicsCollider))
            throw std::runtime_error("Invalid or duplicate engine.physics.collider v1 component");
        NorthStarPhysicsCollider c{};
        std::memcpy(&c,item.payload.data(),sizeof(c));
        auto bounded=[](float f,float min,float max) {
            return std::isfinite(f)&&f>=min&&f<=max;
        };
        if(c.enabled>1 || c.shape>2 ||
           !bounded(c.width,0.01f,100000.f) ||
           !bounded(c.height,0.01f,100000.f) ||
           !bounded(c.depth,0.01f,100000.f) ||
           !bounded(c.offset_x,-100000.f,100000.f) ||
           !bounded(c.offset_y,-100000.f,100000.f) ||
           !bounded(c.offset_z,-100000.f,100000.f))
            throw std::runtime_error("Invalid authored collision shape parameters");
        result=Settings{c.shape,{c.width,c.height,c.depth},
            {c.offset_x,c.offset_y,c.offset_z}};
    }
    return result;
}
}
