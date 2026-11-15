// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once

#include <memory>
#include <optional>
#include <vector>
#include <cstdint>

#include <northstarcpp/scene.hpp>

namespace northstarcpp {

class SceneWorld;
struct PhysicsCastHit {
    float fraction=1.f;
    Vector3 normal{0.f,0.f,0.f};
    std::uint32_t object_index=UINT32_MAX;
};

// Owns a simulation copy. Restarting play creates a new world from the authored scene.
class PhysicsScene {
public:
    explicit PhysicsScene(Scene const& source);
    ~PhysicsScene();

    PhysicsScene(PhysicsScene const&) = delete;
    PhysicsScene& operator=(PhysicsScene const&) = delete;

    Scene const& scene() const;
    std::string provider_name() const;
    std::string provider_version() const;
    northstarcpp::math::float4x4 orientation(std::size_t object_index) const;

    // Elapsed seconds and requested horizontal movement on X and Z in [-1, 1].
    void step(float seconds, float input_x = 0.f, float input_z = 0.f);
    // Gameplay controls remain provider-neutral; both Bullet and Jolt support these.
    void jump();
    void move_static(std::size_t index, Vector3 position);
    // Provider-neutral dynamic-body interaction (Bullet / Jolt).
    void apply_impulse(std::size_t index, Vector3 impulse);
    // Transient script-owned entities use an append-only physics index.
    // The provider must support the optional runtime extension; existing
    // authored bodies retain their indices throughout Play mode.
    bool supports_runtime_entities() const;
    std::size_t spawn_runtime(SceneObject const& object);
    void destroy_runtime(std::size_t index);
    bool runtime_alive(std::size_t index) const;
    std::size_t authored_object_count() const;
    Scene const& authored_scene() const;
    // Reconcile streamed static collision bodies via optional provider API.
    // If the loaded provider lacks the extension, all original collisions
    // remain resident. Terrain and dynamic bodies are never evicted here.
    // Returns the number of changed static bodies.
    std::size_t sync_scene_residency(SceneWorld const& scene_world,
                                     Vector3 protected_position);
    bool supports_scene_residency() const;
    std::size_t ecs_resident_entity_count() const;

    // Provider-neutral convex camera collision query and static triangle mesh
    // registration. Does not mutate Scene::objects or replace authoring assets.
    std::optional<PhysicsCastHit> sweep_sphere(Vector3 start, Vector3 end,
        float radius, std::size_t ignore_index=static_cast<std::size_t>(-1)) const;
    void add_static_mesh(std::vector<Vector3> const& vertices,
                         std::vector<std::uint32_t> const& triangle_indices);

private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace northstarcpp
