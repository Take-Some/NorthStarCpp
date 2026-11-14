// NorthStarCpp - provider-neutral Wavefront OBJ mesh resource
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/scene.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace northstarcpp {
class AssetStore;

struct MeshVertex {
    Vector3 position{};
    Vector3 normal{0.f, 1.f, 0.f};
    float u = 0.f, v = 0.f;
};
struct MeshAsset {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    Vector3 minimum{}, maximum{};
};

// Parses geometry only, without touching the filesystem or provider internals.
// Supports OBJ v, vt, vn, and triangular/convex polygon f records including
// negative vertex indices. Authoring materials require a separate PBR pipeline.
MeshAsset parse_obj_mesh(std::string const& source);
MeshAsset load_obj_mesh(AssetStore const& assets, std::string const& virtual_path);
} // namespace northstarcpp
