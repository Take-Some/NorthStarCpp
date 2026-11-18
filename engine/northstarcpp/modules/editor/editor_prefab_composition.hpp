// NorthStarCpp: editor-only multi-object native Prefab 3D composition.
// Shared software preview renderer consumes the original YDD/YDR triangles,
// UVs, YMAT/YTD materials, source transforms and a shared depth buffer.
#pragma once

#include <northstarcpp/scene.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/material.hpp>
#include <cstddef>
#include <functional>
#include <filesystem>
#include <string>
#include <vector>

namespace northstarcpp {
class AssetStore;
namespace editor::prefab_preview {

struct Composition {
    shared_assets::Drawable drawable;
    std::vector<MaterialDefinition> materials; // one per combined mesh part
    std::size_t visible_objects=0;
    std::size_t modeled_objects=0;
    std::size_t proxy_objects=0;
    std::size_t missing_assets=0;
    std::size_t triangles=0;
    std::vector<std::string> warnings;
};

using DrawableLoader=std::function<
    std::vector<shared_assets::Drawable>(std::string const&)>;
using MaterialLoader=std::function<MaterialDefinition(std::string const&)>;

// Non-mutating, deterministic composition. Scene V3 authored positions are
// already root-relative world coordinates (same contract as instantiate_prefab
// and the current Player renderer); parents express hierarchy, not a second
// transform multiplication. Each actor's Euler XYZ, nonuniform scale, size,
// asset and visibility are applied exactly once.
Composition compose(Scene const& prefab,DrawableLoader const& models,
                    MaterialLoader const& materials);

// Mount the current asset root and the nearest sharedAssets directory once.
// If the browser is already displaying sharedAssets, do not mount it twice.
// Shared by prefab modal preview and asynchronous Content Browser thumbnails.

// Assets are loaded only through the currently active engine.assets contract.
// No direct YDD/YDR parser, OBJ conversion, or physical file bypass.
Composition load_composition(Scene const& prefab,AssetStore const& assets);

} // namespace editor::prefab_preview
} // namespace northstarcpp
