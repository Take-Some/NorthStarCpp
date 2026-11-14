// NorthStarCpp
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// Scene document contracts: legacy v1/v2 and extensible v3.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <northstarcpp/math.hpp>

namespace northstarcpp {

using Vector3 = northstarcpp::math::float3;

// Legacy v1/v2 cap stays at 1024. V3 supports larger partitioned worlds.
inline constexpr std::size_t max_legacy_scene_objects = 1024;
inline constexpr std::size_t max_scene_objects = 16384;

// Scene node metadata is kept distinct from an ECS provider implementation.
// Ids are scene-document stable, not transient ECS handles.
struct SceneNode {
    std::uint64_t id = 0;          // required and unique for v3
    std::uint64_t parent = 0;      // 0 denotes a root node
    std::string layer = "Default";
    std::string cell;              // empty => persistent entity
    std::string room;              // empty => exterior
    std::string asset;             // virtual Asset Manager path (not a filesystem path)
    Vector3 rotation{0.f, 0.f, 0.f}; // Euler degrees, authored local transform
    Vector3 scale{1.f, 1.f, 1.f};
    float lod_near = 0.f;
    float lod_far = 1000000.f;
    bool visible = true;
    bool collision = true;
    // Optional per-entity JavaScript component; project-relative VFS path.
    std::string script; // scripts/*.js, v3 only
};

struct SceneObject {
    std::string name = "Cube";
    Vector3 position{0.f, 0.f, 0.f};
    float size = 1.f;
    std::uint32_t color = 0x46b9d8;
    bool player = false;
    float mass = 0.f; // kilograms; zero makes a static body

    // The scene runtime preserves this authoring metadata through ECS editing.
    SceneNode node{};
};

struct SceneLayer {
    std::string name = "Default";
    bool visible = true;
};

struct SceneCell {
    std::string name;
    Vector3 minimum{0.f, 0.f, 0.f};
    Vector3 maximum{0.f, 0.f, 0.f};
    bool always_loaded = false;
};

struct SceneVolume {
    enum class Shape { sphere, box };
    std::string name;
    Shape shape = Shape::sphere;
    Vector3 minimum{0.f, 0.f, 0.f}; // box min or sphere center
    Vector3 maximum{0.f, 0.f, 0.f}; // box max; ignored for sphere
    float radius = 0.f;
    std::string tag; // e.g. audio, weather, trigger, streaming_source
    bool enabled = true;
};

struct ScenePortal {
    std::string name;
    std::string room_a;
    std::string room_b;
    Vector3 minimum{0.f, 0.f, 0.f};
    Vector3 maximum{0.f, 0.f, 0.f};
    bool open = true;
};

struct SceneStreamingSettings {
    float load_radius = 150.f;
    float unload_radius = 220.f; // hysteresis: must exceed load_radius
    float lookahead_seconds = 1.f;

    // Streaming work is amortized across frames. Mandatory/pinned cells load
    // immediately; ordinary distance-driven transitions obey these budgets.
    // Defaults preserve all existing small scenes and legacy v3 documents.
    std::uint32_t max_cell_loads_per_update = 64;
    std::uint32_t max_cell_unloads_per_update = 64;
};

// Opaque ECS component data is retained when its provider is unavailable.
// Type is a stable reverse-DNS identifier (e.g. "com.example.weather").
// Runtime entity IDs are not serialized; only authored stable IDs are used.
struct SceneComponentBlob {
    std::uint64_t entity_id = 0;  // 0: scene-level component
    std::string type;
    std::uint32_t version = 1;
    std::string payload;          // arbitrary binary, up to 1 MiB
};

// Future/third-party record kinds can survive a read/edit/write cycle even
// when the current engine does not understand their payload.
struct SceneOpaqueRecord {
    std::uint16_t kind = 0;
    std::uint16_t version = 1;
    std::uint64_t entity_id = 0;
    std::string payload;
};

struct Scene {
    std::vector<SceneObject> objects;
    // Extra metadata is opt-in. Existing scenes default to v2 automatically.
    std::uint32_t version = 2;
    std::string name = "Untitled";
    std::vector<SceneLayer> layers;
    std::vector<SceneCell> cells;
    std::vector<SceneVolume> volumes;
    std::vector<ScenePortal> portals;
    SceneStreamingSettings streaming{};
    std::vector<SceneComponentBlob> components;
    std::vector<SceneOpaqueRecord> opaque_records;
};

Scene make_startup_scene();
Scene load_scene(std::filesystem::path const& path);
Scene parse_scene_data(std::string const& bytes);
void save_scene(Scene const& scene, std::filesystem::path const& path);
void validate_scene(Scene const& scene);

// Legacy text V3 remains a compatibility import/export codec only.
// Current production scene files use NEF8 chunked ListFile encoding.
std::string serialize_scene_binary(Scene const& scene);
Scene parse_scene_binary(std::string const& bytes);

// V3 is human-readable, deterministic, and deliberately independent of
// the proprietary source implementation used as the architectural reference.
std::string serialize_scene_v3(Scene const& scene);
Scene parse_scene_v3(std::string const& bytes);

} // namespace northstarcpp
