// NorthStarCpp: triangle-accurate Qt Editor selection using engine.collision.
// Geometry and collision are supplied by the current replaceable engine ABI.
#include "editor_camera.hpp"
#include "editor_tools.hpp"
#include <northstarcpp/collision_service.hpp>
#include <northstarcpp/geometry_service.hpp>
#include <northstarcpp/geometry_scene.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace northstarcpp::editor_tools {
std::optional<ActorSurfaceHit> hit_actor_geometry(
    Scene const& scene, Ray const ray, CollisionService const& collision,
    GeometryService const& geometry) {
    if (!std::isfinite(ray.direction.x) ||
        !std::isfinite(ray.direction.y) ||
        !std::isfinite(ray.direction.z))
        return {};
    float best = std::numeric_limits<float>::max();
    std::optional<ActorSurfaceHit> chosen;
    for (std::size_t i = 0; i < scene.objects.size(); ++i) {
        auto const& object = scene.objects[i];
        if (scene.version == 3 && !object.node.visible)
            continue;

        // Procedural objects have an authored, versioned geometry component.
        // No AABB hit can select a hole in a ring or a torus.
        NorthStarGeometryRequest request{};
        bool procedural = false;
        if (scene.version == 3 && object.node.id != 0) {
            for (auto const& component : scene.components) {
                if (component.entity_id == object.node.id &&
                    decode_geometry_primitive_component(component, request)) {
                    procedural = true;
                    break;
                }
            }
        }
        if (!procedural) {
            // Ordinary scene proxies keep their established oriented-box
            // picking. Mesh selection never downgrades procedural misses.
            auto distance = intersect_actor_proxy(ray, object, scene.version);
            if (distance && *distance < best) {
                best = *distance;
                chosen = ActorSurfaceHit{i,*distance,{
                    ray.origin.x+ray.direction.x*(*distance),
                    ray.origin.y+ray.direction.y*(*distance),
                    ray.origin.z+ray.direction.z*(*distance)}};
            }
            continue;
        }
        if (request.kind == NORTHSTAR_GEOMETRY_WIRE)
            continue; // Wire edges need a separate line tolerance query.
        auto mesh = geometry.generate(request);
        if (mesh.indices.empty() || mesh.xyz.empty())
            continue;
        if (mesh.indices.size() % 3 || mesh.xyz.size() % 3)
            throw std::runtime_error("Malformed engine.geometry triangle mesh");
        if (object.size <= 0.f || !std::isfinite(object.size))
            continue;
        auto const axis_x=gizmo_axis(object,GizmoAxis::x,true);
        auto const axis_y=gizmo_axis(object,GizmoAxis::y,true);
        auto const axis_z=gizmo_axis(object,GizmoAxis::z,true);
        float scale_x=object.size*.5f;
        float scale_y=object.size*.5f;
        float scale_z=object.size*.5f;
        if (scene.version == 3) {
            scale_x *= object.node.scale.x;
            scale_y *= object.node.scale.y;
            scale_z *= object.node.scale.z;
        }
        std::vector<float> world;
        world.reserve(mesh.xyz.size());
        for (std::size_t v=0;v<mesh.xyz.size();v+=3) {
            float x=mesh.xyz[v]*scale_x;
            float y=mesh.xyz[v+1]*scale_y;
            float z=mesh.xyz[v+2]*scale_z;
            world.push_back(object.position.x + axis_x.x*x + axis_y.x*y + axis_z.x*z);
            world.push_back(object.position.y + object.size*.5f +
                axis_x.y*x + axis_y.y*y + axis_z.y*z);
            world.push_back(object.position.z + axis_x.z*x + axis_y.z*y + axis_z.z*z);
        }
        auto hit=collision.ray_mesh(ray.origin,ray.direction,
            world,mesh.indices,best);
        if (hit && hit->distance < best) {
            best=hit->distance;
            chosen=ActorSurfaceHit{i,hit->distance,{
                ray.origin.x+ray.direction.x*hit->distance,
                ray.origin.y+ray.direction.y*hit->distance,
                ray.origin.z+ray.direction.z*hit->distance}};
        }
    }
    return chosen;
}
std::optional<std::size_t> pick_actor_geometry(
    Scene const& scene,Ray ray,CollisionService const& collision,
    GeometryService const& geometry) {
    auto hit=hit_actor_geometry(scene,ray,collision,geometry);
    return hit?std::optional<std::size_t>(hit->index):std::nullopt;
}
} // namespace northstarcpp::editor_tools
