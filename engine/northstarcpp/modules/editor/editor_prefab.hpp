// NorthStarCpp editor prefab authoring contract.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/scene.hpp>
#include <cstddef>
#include <cstdint>

namespace northstarcpp::editor_tools {

// Extract a selected root and all descendants into a standalone Scene V3
// document; positions and streaming-cell bounds become root-relative.
// The source authoring scene is never modified.
Scene capture_prefab(Scene const& source, std::size_t root_index);

// Instantiate a validated prefab as regular Scene V3 entities. New stable IDs
// are assigned, parent links remapped, missing layers/cells imported safely.
// The source template remains unchanged and every failure is transactional.
// Existing scene nodes are unaffected. Root is attached to parent_id (or 0).
// Returns the new root index.
std::size_t instantiate_prefab(Scene& destination, Scene const& prefab,
                               Vector3 anchor, std::uint64_t parent_id = 0);
} // namespace northstarcpp::editor_tools
