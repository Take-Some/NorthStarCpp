// NorthStarCpp - provider-neutral Wavefront OBJ mesh resource
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/scene.hpp>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace northstarcpp {
class AssetStore;

struct MeshVertex {
    Vector3 position{};
    Vector3 normal{0.f, 1.f, 0.f};
    float u = 0.f, v = 0.f;
};
// One material-bound draw range in the native YDD/YDR model. An authored
// scene material override can replace the reference without losing the
// drawable's original group topology or UV coordinates.
struct MeshSection {
    std::string name;
    std::string material_reference;
    std::size_t first_index = 0;
    std::size_t index_count = 0;
    bool material_required = true;
};
struct MeshAsset {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshSection> sections; // Empty for plain OBJ without material groups.
    Vector3 minimum{}, maximum{};
};

// Parses geometry only, without touching the filesystem or provider internals.
// Supports OBJ v, vt, vn, and triangular/convex polygon f records including
// negative vertex indices. Authoring materials require a separate PBR pipeline.
MeshAsset parse_obj_mesh(std::string const& source);
MeshAsset load_obj_mesh(AssetStore const& assets, std::string const& virtual_path);
// Runtime native YDD/YDR/OBJ mesh dispatch; all inputs resolve through VFS.
// Native binary model material/skin support is handled by the render pipeline.
MeshAsset load_scene_mesh(AssetStore const& assets, std::string const& reference);

} // namespace northstarcpp
