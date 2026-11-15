#pragma once
// D3D11 primitive CPU mesh generation; no device context ownership.
#include <northstarcpp/core/math_standalone.h>
#include <cstdint>
#include <vector>
namespace northstarcpp::render::d3d11::geometry {
struct Vertex {
    float x, y, z, r, g, b;
    float surface=0.f; // 0 untextured; 1..3 world material triplets
};
void add_cube(std::vector<Vertex>& vertices,
              const northstarcpp::math::float4x4& transform,std::uint32_t color);
}
