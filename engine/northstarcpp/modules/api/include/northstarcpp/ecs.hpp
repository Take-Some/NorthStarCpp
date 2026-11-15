// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once
#include <northstarcpp/ecs_api.h>
#include <northstarcpp/scene.hpp>
#include <memory>
#include <string>
#include <vector>

namespace northstarcpp {
class SceneWorld;

/* Provider-independent authoritative entity world. Scene is only a file/render DTO. */
class EcsWorld {
public:
    EcsWorld();
    ~EcsWorld();
    EcsWorld(EcsWorld const&) = delete;
    EcsWorld& operator=(EcsWorld const&) = delete;
    EcsWorld(EcsWorld&&) noexcept;
    EcsWorld& operator=(EcsWorld&&) noexcept;

    NsEntity create(SceneObject const& object);
    // Append one transient entity without rebuilding or validating the
    // entire authored document. The scene host owns stable-ID uniqueness.
    NsEntity create_runtime(SceneObject const& object);
    void update(NsEntity entity, SceneObject const& object);
    void erase(NsEntity entity);
    bool alive(NsEntity entity) const;
    SceneObject object(NsEntity entity) const;
    std::size_t size() const;
    NsEntity at(std::size_t index) const;
    std::vector<NsEntity> entities() const;
    Scene snapshot() const;
    void load(Scene const& scene);
    void sync_positions(Scene const& scene);
    // Reconcile actual Flecs/provider entities to decoded World Partition
    // residency using stable authored scene IDs. Dynamic entities stay alive;
    // streamed static handles are re-created on return to the same cell.
    std::size_t reconcile_scene_residency(Scene const& source,
                                          SceneWorld const& world);
    void step(float seconds, float input_x = 0.f, float input_z = 0.f);
    std::string provider_name() const;

    /* Generic ECS extension API for game modules and editor tooling. */
    void register_component(std::string const& key, std::size_t bytes, std::size_t align);
    void set_component(NsEntity entity, std::string const& key, void const* data, std::size_t bytes);
    void get_component(NsEntity entity, std::string const& key, void* data, std::size_t bytes) const;
    void remove_component(NsEntity entity, std::string const& key);
    bool has_component(NsEntity entity, std::string const& key) const;
    std::vector<NsEntity> query(std::vector<std::string> const& required) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Backward-compatible name for older integrations; it uses the same neutral API.
using EcsSceneRuntime = EcsWorld;
} // namespace northstarcpp
