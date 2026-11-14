// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <northstarcpp/math.hpp>

namespace northstarcpp::geometry {

enum class SolidPrimitive { cube, rectangle, cone, cylinder, ellipsoid };
enum class WirePrimitive { box, rectangle, cone, ellipsoid, ellipse };

// Each index addresses a vertex stored as three consecutive floats.
// Views refer to immutable engine data and are valid for the life of the process.
struct PrimitiveView {
    const float* positions;
    std::size_t vertex_count;
    const std::uint16_t* indices;
    std::size_t index_count;
};

PrimitiveView primitive(SolidPrimitive shape); // triangle list
PrimitiveView primitive(WirePrimitive shape);  // line list

struct Mesh {
    std::vector<math::float3> vertices;
    std::vector<std::uint16_t> indices; // triangle list
};

// Scale the native primitive coordinates before applying the affine transform.
Mesh make_mesh(SolidPrimitive shape, math::float3 const& scale, math::float4x4 const& transform);

// Radii and width use world units. Each segment count must be at least three.
// A mesh can address up to 65,536 vertices using its 16-bit indices.
Mesh make_ring(float inner_radius, float width, std::uint16_t segments);
Mesh make_torus(float major_radius, float minor_radius, std::uint16_t major_segments,
    std::uint16_t minor_segments, math::float4x4 const& transform);

} // namespace northstarcpp::geometry
