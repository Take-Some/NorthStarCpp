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
    std::vector<std::string> unresolved_materials; // Import diagnostics for fallback slots.
    std::string skeleton_reference;
    std::optional<Skeleton> skeleton;
    std::vector<SkinWeights> vertex_skin; // Same order as mesh.vertices.
    math::float4x4 mesh_to_skeleton{};
    bool has_skin() const noexcept {
        return skeleton.has_value() && vertex_skin.size()==mesh.vertices.size();
    }
};

// Imported YDD vertices are mesh-local; YMT/YCD joint palettes are
// skeleton-local. Row-vector skin transform is:
// mesh_to_skeleton * skeleton_palette * skeleton_to_mesh.
// Reuse the destination buffer to avoid per-frame palette allocations.
void remap_skin_palette_to_mesh(
    std::vector<math::float4x4>& output,
    std::vector<math::float4x4> const& skeleton_palette,
    math::float4x4 const& mesh_to_skeleton);

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
    std::vector<math::float4x4> const& palette,
    std::vector<std::uint8_t> const* active_vertices = nullptr);



// Reusable-frame CPU skinning: the destination retains its topology and UVs.
// No per-frame copy of hundreds of thousands of indices/material sections.
void skin_native_mesh_cpu_into(
    MeshAsset& output, MeshAsset const& mesh, Skeleton const& skeleton,
    std::vector<SkinWeights> const& weights,
    std::vector<math::float4x4> const& palette,
    std::vector<std::uint8_t> const* active_vertices = nullptr);

// Strict by default. Optional viewport fallback keeps geometry when YMAT is absent.
// The fallback is neutral PBR, and missing dependencies are reported.
NativeModelResource load_native_model_resource(
    AssetStore const& assets, std::string const& reference,
    bool allow_missing_materials=false);
} // namespace northstarcpp
