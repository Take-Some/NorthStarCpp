// NorthStarCpp native YDD/YDR resource bundle.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
// Native game assets never require an OBJ conversion step.
#pragma once
#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/material.hpp>
#include <northstarcpp/skeleton.hpp>
#include <northstarcpp/character.hpp>
#include <optional>
#include <string>
#include <unordered_map>

namespace northstarcpp {
class AssetStore;

// Provider-neutral, CPU-side dependency bundle for one VFS drawable.
// Each unique YMAT@entry is resolved once into MaterialDefinition with YTD
// images. A companion YMT enables decoded original 8-weight influences.
// Static models work without a rig. Skin data retains the original vertex
// order and can be used by both the CPU reference and a GPU provider.
struct NativeModelResource {
    MeshAsset mesh;
    std::unordered_map<std::string,MaterialDefinition> materials;
    std::string skeleton_reference;
    std::optional<Skeleton> skeleton;
    std::vector<SkinWeights> vertex_skin; // Same order as mesh.vertices.
    math::float4x4 mesh_to_skeleton{};
    bool has_skin() const noexcept {
        return skeleton.has_value() && vertex_skin.size()==mesh.vertices.size();
    }
};

// CPU reference for native YDD 8-weight skinning; preserves the original
// UV/material section topology for PBR. GPU path must match its output.
MeshAsset skin_native_model_cpu(
    NativeModelResource const& model,
    std::vector<math::float4x4> const& palette);
// Efficient overload for runtime: geometry and rig stay cached separately.
// A frame allocates only its deformed mesh, never copies YTD image payloads.
MeshAsset skin_native_mesh_cpu(
    MeshAsset const& mesh, Skeleton const& skeleton,
    std::vector<SkinWeights> const& weights,
    std::vector<math::float4x4> const& palette);



// Strict, bounded native model load. Missing required materials fail the
// bundle rather than creating untextured placeholder geometry.
NativeModelResource load_native_model_resource(
    AssetStore const& assets, std::string const& reference);
} // namespace northstarcpp
