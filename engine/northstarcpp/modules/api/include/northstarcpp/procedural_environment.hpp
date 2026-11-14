// Native procedural generation using the engine.perlin provider.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/scene.hpp>
#include <cstdint>
#include <vector>

namespace northstarcpp {
struct CloudNoiseSettings {
    std::uint64_t seed = 2026;
    std::uint32_t width = 128;
    std::uint32_t height = 128;
};
// Tileable RGBA8 noise for the D3D11 skydome's triplanar cloud shader.
// Generates three independent frequency bands, not a baked RGB sky photograph.
std::vector<std::uint8_t> make_perlin_cloud_rgba(CloudNoiseSettings settings = {});

struct PerlinWorldSettings {
    std::uint64_t seed = 2026;
    std::uint32_t columns = 24;
    std::uint32_t rows = 24;
    std::uint32_t cell_tiles = 8;
    float tile_size = 2.0f;
    std::uint32_t max_layers = 5;
};
// An opt-in Scene V3 (collidable cube terrain, stable entity IDs and streaming
// cells), independently renderable/editable with existing scene APIs.
Scene make_perlin_world(PerlinWorldSettings settings = {});
} // namespace northstarcpp
