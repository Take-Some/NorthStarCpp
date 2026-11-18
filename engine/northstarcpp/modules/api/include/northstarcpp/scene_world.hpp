// NorthStarCpp
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#pragma once

#include <northstarcpp/scene.hpp>
#include <northstarcpp/scene_cost_profiler.hpp>

#include <array>
#include <cstddef>
#include <optional>
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

    struct ObjectBounds {
        std::uint64_t id = 0;
        Vector3 position{0.f, 0.f, 0.f};
        float half_extent = 0.f;
    };
    std::vector<ObjectBounds> objects_;
    std::unordered_map<CellKey, std::vector<std::size_t>, Hash> cells_;
    std::vector<CellKey> object_cells_;
    // Dense-bucket indices allow O(1) removal when moving between cells.
    std::vector<std::size_t> object_slots_;
    // Reused validation scratch avoids allocating per live update.
    std::vector<CellKey> staged_cells_;
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
    std::size_t cell_selection_passes = 0;
    std::size_t portals_frustum_rejected = 0;
    std::uint64_t last_update_cpu_us = 0; // SceneWorld CPU only; not GPU time
};

// Runtime HLOD proxy for static scene groups; collision always remains in
// the original physics bodies. Geometry is an explicit authored-world AABB
// approximation, not a generated mesh-LOD asset.
// Plane half-space is dot(normal, position) + offset >= 0.
// A camera frustum is optional, preserving old portal reachability behavior.
struct SceneViewPlane {
    Vector3 normal{0.f, 0.f, 1.f};
    float offset = 0.f;
};
struct SceneViewFrustum {
    std::array<SceneViewPlane, 6> planes{};
};

// CPU authoring and streaming counters; no unmeasured GPU/VRAM estimates.
struct SceneCellTelemetry {
    std::string name;
    std::size_t authored_entities = 0;
    std::size_t unique_authored_assets = 0;
    std::size_t assigned_entities = 0;
    std::size_t resident_entities = 0;
    std::size_t visible_entities = 0;
    std::size_t active_hlod_proxies = 0;
    std::size_t hlod_replaced_entities = 0;
    bool loaded = false;
    bool payload_ready = false;
};

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
    // Conservative portal-volume camera-frustum gate (optional).
    void set_view_frustum(SceneViewFrustum const& frustum);
    void clear_view_frustum() noexcept { view_frustum_.reset(); }

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
    std::vector<SceneCellTelemetry> const& cell_telemetry() const { return cell_telemetry_; }
    // Per-scene rolling CPU profile; GPU timing/bytes are optional and must
    // be fed by the renderer from actual timestamp and memory queries.
    SceneCostProfiler& cost_profiler() const {return cost_profiler_;}

private:
    struct CellCandidate {
        std::size_t index;
        float priority;
        bool forced;
    };
    void update_cell_selection(Vector3 position,Vector3 predicted);
    std::vector<CellCandidate> cell_loads_,cell_unloads_;
    std::size_t next_load_=0,next_unload_=0;
    Vector3 selection_position_{0.f,0.f,0.f},selection_prediction_{0.f,0.f,0.f};
    bool selection_valid_=false;
    std::vector<std::uint8_t> hierarchy_state_;

    Scene scene_;
    SceneSpatialIndex spatial_;
    std::unordered_map<std::string, std::size_t> cell_lookup_;
    std::unordered_map<std::uint64_t, std::size_t> id_lookup_;
    // Static adjacency avoids scanning every portal for each reachable room.
    std::unordered_map<std::string, std::vector<std::size_t>> portals_by_room_;
    std::unordered_map<std::string, std::vector<Vector3>> portal_polygons_;
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
    mutable SceneCostProfiler cost_profiler_{};
    std::vector<SceneCellTelemetry> cell_telemetry_;
    std::optional<SceneViewFrustum> view_frustum_;
    std::size_t generation_ = 0;
};

} // namespace northstarcpp
