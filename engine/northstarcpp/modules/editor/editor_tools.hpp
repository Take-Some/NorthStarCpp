// NorthStarCpp Editor tools
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#pragma once

#include <northstarcpp/scene.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace northstarcpp::editor_tools {

// Scene-level, provider-independent editor transform behavior.
enum class GizmoMode { translate, rotate, scale };
enum class GizmoAxis { x, y, z };
struct GizmoOptions {
    bool snap = true;
    bool local = false;
    float translation_step = 1.f;
    float rotation_step = 15.f;
    float scale_step = 0.25f;
};

// Returns a normalized world-space axis. Translation can use an object's local
// Euler frame; the authored rotation and scale controls remain axis-specific.
Vector3 gizmo_axis(SceneObject const& object, GizmoAxis axis, bool local);
void apply_gizmo(Scene& scene, std::size_t index, GizmoMode mode,
                 GizmoAxis axis, float amount, GizmoOptions const& options);

// Edits are validated through the same scene API used by the game and exporter.
// The ECS provider remains the source of truth between authoring transactions.
void promote_to_v3(Scene& scene);
std::size_t duplicate(Scene& scene, std::size_t index, bool as_child = false);
std::size_t delete_subtree(Scene& scene, std::size_t index);
std::size_t create_layer(Scene& scene, std::size_t selected_index);
std::size_t create_cell(Scene& scene, std::size_t selected_index);
void create_volume(Scene& scene, std::size_t selected_index);
void create_portal(Scene& scene, std::size_t selected_index);
std::string hierarchy_label(Scene const& scene, std::size_t index);

// Snapshots are restored via EcsWorld::load, not through a Flecs-specific API.
// Keep bounded history to prevent unbounded editor memory consumption.
class History {
public:
    void clear() noexcept;
    void record(Scene before, std::size_t selected_index);
    bool undo(Scene current, std::size_t selected_index,
              Scene& restored, std::size_t& restored_selection);
    bool redo(Scene current, std::size_t selected_index,
              Scene& restored, std::size_t& restored_selection);
    bool can_undo() const noexcept { return !undo_.empty(); }
    bool can_redo() const noexcept { return !redo_.empty(); }
private:
    struct Checkpoint { Scene scene; std::size_t selected_index; };
    static constexpr std::size_t limit = 32;
    std::vector<Checkpoint> undo_, redo_;
};
} // namespace northstarcpp::editor_tools
