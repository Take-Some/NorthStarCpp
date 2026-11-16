// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once

#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <cstdint>

#include <northstarcpp/scene.hpp>
#include <northstarcpp/physics_vehicle_plugin.h>

namespace northstarcpp {

class SceneWorld;
struct PhysicsCastHit {
    float fraction=1.f;
    Vector3 normal{0.f,0.f,0.f};
    std::uint32_t object_index=UINT32_MAX;
    // Optional Jolt surface extension; unknown if physics provider lacks it.
    std::uint32_t material_id=0;
    std::uint32_t triangle_index=UINT32_MAX;
    std::uint32_t response=2; // Block=2, Overlap=1
};

struct PlayerAnimationFrame {
    std::uint64_t pawn_id=0;
    std::uint64_t mesh_id=0;
    std::string state;
    std::string clip;
    std::string previous_clip;
    float blend_alpha=1.f;
    float playback_seconds=0.f;
    bool grounded=true;
};
struct PhysicsContactEvent {
    std::uint32_t first_index=UINT32_MAX;
    std::uint32_t second_index=UINT32_MAX;
    std::uint32_t phase=0;    // 0 Begin, 1 End
    std::uint32_t response=2; // 1 Overlap, 2 Block
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
    // Canonical PlayerController -> provider handoff; stable Scene V3 IDs.
    void possess(std::uint64_t entity_id);
    void unpossess();
    std::uint64_t controlled_entity() const noexcept;
    std::optional<std::size_t> controlled_index() const noexcept;
    void set_sprinting(bool active);
    std::optional<PlayerAnimationFrame> player_animation() const;
    void move_static(std::size_t index, Vector3 position);
    // Authored, non-colliding visual rotation does not change Pawn or rigidbody.
    void rotate_authored_visual(std::size_t index, Vector3 euler_degrees);
    // Visual-only authored rotation for a live, noncolliding runtime entity.
    void rotate_runtime(std::size_t index, Vector3 euler_degrees);
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
    bool supports_collision_response_matrix() const;
    std::vector<PhysicsContactEvent> poll_collision_events(std::size_t max_events=1024);
    std::size_t ecs_resident_entity_count() const;

    // Provider-neutral convex camera collision query and static triangle mesh
    // registration. Does not mutate Scene::objects or replace authoring assets.
    std::optional<PhysicsCastHit> sweep_sphere(Vector3 start, Vector3 end,
        float radius, std::size_t ignore_index=static_cast<std::size_t>(-1)) const;
    void add_static_mesh(std::vector<Vector3> const& vertices,
                         std::vector<std::uint32_t> const& triangle_indices);

    // Queries use a 0..15 project collision channel and respect Block/Overlap/Ignore.
    std::optional<PhysicsCastHit> sweep_sphere_channel(
        Vector3 start,Vector3 end,float radius,std::uint32_t query_channel,
        bool include_overlaps=false,
        std::size_t ignore_index=static_cast<std::size_t>(-1)) const;
    std::optional<PhysicsCastHit> raycast_channel(
        Vector3 start,Vector3 end,std::uint32_t query_channel,
        bool include_overlaps=false,
        std::size_t ignore_index=static_cast<std::size_t>(-1)) const;

    // Optional Jolt physics surface material extension.
    bool supports_surface_materials() const;
    void add_static_mesh(std::vector<Vector3> const& vertices,
                         std::vector<std::uint32_t> const& triangle_indices,
                         std::vector<std::uint32_t> const& material_ids);

    // Optional Jolt 6-DOF native vehicle dynamics.
    bool supports_native_vehicle() const;
    void create_native_vehicle(std::size_t index,NorthStarVehicleRig const& rig,float yaw);
    void destroy_native_vehicle(std::size_t index);
    void native_vehicle_controls(std::size_t index,float throttle,
                                 float steering,float brake,float handbrake=0.f);
    NorthStarPhysicsVehiclePose native_vehicle_pose(std::size_t index) const;
    NorthStarVehicleWheelState native_vehicle_wheel(std::size_t index,std::uint32_t wheel) const;
    std::uint32_t native_vehicle_wheel_count(std::size_t index) const;

private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace northstarcpp
