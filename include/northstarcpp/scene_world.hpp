// NorthStarCpp
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#pragma once

#include <northstarcpp/scene.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace northstarcpp {

// A read-only spatial index that avoids visiting every authored entity when
// querying bounded regions. Index is intentionally not tied to Flecs/Bullet.
class SceneSpatialIndex {
public:
    explicit SceneSpatialIndex(Scene const& scene, float grid_size = 64.f);

    // Results are original Scene::objects indices, in ascending index order.
    std::vector<std::size_t> query_sphere(Vector3 center, float radius) const;
    std::vector<std::size_t> query_box(Vector3 minimum, Vector3 maximum) const;
    // Incrementally reindex moving entities without rebuilding the authored
    // hierarchy or invalidating source scene indices.
    void update_dynamic_positions(Scene const& live_scene);

private:
    struct CellKey {
        int x = 0, z = 0;
        bool operator==(CellKey const& other) const {
            return x == other.x && z == other.z;
        }
    };
    struct Hash {
        std::size_t operator()(CellKey const& key) const;
    };

    std::vector<SceneObject> objects_;
    std::unordered_map<CellKey, std::vector<std::size_t>, Hash> cells_;
    std::vector<CellKey> object_cells_;
    float grid_size_ = 64.f;
    float maximum_extent_ = 0.f;
};

// Per-update transitions are diagnostic notices, not an order to destroy
// physics bodies or GPU allocations. Residency remains owned by each provider.
struct SceneCellTransition {
    std::string name;
    bool loaded = false;
    bool forced = false; // pin / always_loaded bypasses the regular load budget
    float priority = 0.f;
};

struct SceneStreamingDiagnostics {
    std::size_t loaded_cells = 0;
    std::size_t pending_loads = 0;
    std::size_t pending_unloads = 0;
    std::size_t resident_entities = 0;
    std::size_t visible_entities = 0;
    std::size_t generation = 0;
};

// Runtime HLOD proxy for static scene groups; collision always remains in
// the original physics bodies. Geometry is an explicit authored-world AABB
// approximation, not a generated mesh-LOD asset.
struct SceneHlodProxy {
    Vector3 center{0.f,0.f,0.f};
    Vector3 half_extent{1.f,1.f,1.f};
    std::uint32_t color=0x708090;
    std::uint32_t level=0;
    std::size_t member_count=0;
    std::vector<std::size_t> members;
};

// Spatial streaming, layer visibility, interior portal traversal and LOD.
// Frame-update decisions are deterministic, side-effect-free for ECS, and
// expose original indices for physics/render selection without reordering.
class SceneWorld {
public:
    explicit SceneWorld(Scene scene);

    void update(Vector3 camera_position, Vector3 camera_velocity = {0.f, 0.f, 0.f},
                std::string current_room = {},
                Scene const* live_scene = nullptr);
    void set_layer_visible(std::string const& name, bool visible);
    void set_portal_open(std::string const& name, bool open);
    void pin_cell(std::string const& name, bool resident);

    bool cell_loaded(std::string const& name) const;
    // When enabled, disk-backed cells are not renderable/instantiated before
    // their verified NEF8 payload has arrived. Logical cell loading is still
    // used to drive I/O scheduling. Dynamic entities remain pinned.
    void enable_payload_gate(bool enabled);
    void set_cell_payload_ready(std::string const& name, bool ready);
    bool cell_payload_ready(std::string const& name) const;
    bool object_visible(std::size_t source_index) const;
    bool object_resident(std::size_t source_index) const;
    // Runtime ownership may differ from authored ownership after an entity
    // moves between sectors; the authored SceneNode is not overwritten.
    std::string effective_cell_for_object(std::size_t source_index) const;
    std::vector<std::size_t> const& visible_indices() const { return visible_indices_; }
    // Two spatial HLOD tiers (64m / 256m). Runtime-selected proxies are
    // returned separately, preserving ECS and physics object indexing.
    std::vector<SceneHlodProxy> const& hlod_proxies() const { return hlod_proxies_; }
    std::size_t hlod_cluster_count() const { return hlod_clusters_.size(); }
    std::vector<std::size_t> const& resident_indices() const { return resident_indices_; }

    // Virtual assets to prefetch via AssetManager; no direct disk access.
    // Priority order: active high-detail first, prefetched next.
    std::vector<std::string> asset_dependencies() const;
    std::vector<std::size_t> query_sphere(Vector3 center, float radius) const;
    std::vector<std::size_t> query_box(Vector3 minimum, Vector3 maximum) const;
    bool volume_contains(std::string const& volume_name, Vector3 position) const;

    Scene const& source() const { return scene_; }
    std::size_t loaded_cell_count() const;
    std::size_t update_generation() const { return generation_; }
    // Ordered load/unload transitions from the last update.
    std::vector<SceneCellTransition> const& cell_transitions() const { return transitions_; }
    SceneStreamingDiagnostics const& streaming_diagnostics() const { return diagnostics_; }

private:
    Scene scene_;
    SceneSpatialIndex spatial_;
    std::unordered_map<std::string, std::size_t> cell_lookup_;
    std::unordered_map<std::uint64_t, std::size_t> id_lookup_;
    std::unordered_set<std::string> pinned_;
    std::vector<std::uint8_t> loaded_cells_;
    std::vector<std::uint8_t> payload_ready_;
    bool payload_gate_ = false;
    std::vector<std::size_t> effective_cell_indices_;
    std::vector<Vector3> last_live_positions_;
    std::vector<std::uint8_t> resident_mask_;
    std::vector<std::uint8_t> visible_mask_;
    std::vector<std::size_t> resident_indices_;
    std::vector<std::size_t> visible_indices_;
    std::vector<SceneHlodProxy> hlod_clusters_;
    std::vector<SceneHlodProxy> hlod_proxies_;
    std::vector<SceneCellTransition> transitions_;
    SceneStreamingDiagnostics diagnostics_{};
    std::size_t generation_ = 0;
};

} // namespace northstarcpp
