// NorthStar Editor — camera and geometry-accurate authoring picking.
// Copyright (c) 2026 Kayla's Systems — Kayla Verner.
#pragma once
#include <northstarcpp/camera.hpp>
#include <northstarcpp/scene.hpp>
#include <cstddef>
#include <optional>

namespace northstarcpp { class CollisionService; class GeometryService; }
namespace northstarcpp::editor_tools {
struct ScreenPoint {float x=0.f,y=0.f;};
struct Ray {Vector3 origin{},direction{0.f,0.f,1.f};};
struct EditorCamera {
    CameraPose pose{{8.f,7.f,-10.f}, -0.67474094f, -0.5f,0.f,45.f,0.1f,30000.f};
    Vector3 forward() const noexcept;
    Vector3 right() const noexcept;
    Vector3 up() const noexcept;
    void look(float delta_x,float delta_y,float sensitivity=0.004f) noexcept;
    void move(float sideways,float vertical,float forward_distance) noexcept;
    // Editor fly navigation uses world-up for Q/E while forward follows
    // camera pitch. Keeps world vertical independent of the view roll.
    void fly(float sideways,float world_up,float forward_distance) noexcept;
    void dolly(float amount) noexcept;
    void frame(Vector3 center,float distance=14.f) noexcept;
    std::optional<ScreenPoint> project(Vector3 world,float width,float height) const noexcept;
    Ray screen_ray(float x,float y,float width,float height) const noexcept;
};
// Ray hit is in metres along a unit-length ray. Uses oriented, scaled boxes
// matching the authored cube proxy; depth order resolves occlusion.
std::optional<float> intersect_actor_proxy(Ray ray,SceneObject const& object,
                                           std::uint32_t scene_version) noexcept;
// Exact triangle picking for procedurally generated scene meshes.
std::optional<std::size_t> pick_actor(Scene const& scene,Ray ray) noexcept;
std::optional<std::size_t> pick_actor_geometry(
    Scene const& scene, Ray ray,
    northstarcpp::CollisionService const& collision,
    northstarcpp::GeometryService const& geometry);
} // namespace northstarcpp::editor_tools
