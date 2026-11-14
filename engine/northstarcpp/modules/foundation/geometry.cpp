// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/geometry.hpp>
#include <northstarcpp/compat/core/math_standalone.h>
#include <northstarcpp/compat/geometry_primitives.h>

#include <cmath>
#include <stdexcept>

namespace northstarcpp::geometry {

PrimitiveView primitive(SolidPrimitive shape)
{
    using namespace geometry_utils;
    switch (shape) {
    case SolidPrimitive::cube:
        return {cube_solid::vertices, cube_solid::vertex_count, cube_solid::faces, cube_solid::index_count};
    case SolidPrimitive::rectangle:
        return {rectangle_solid::vertices, rectangle_solid::vertex_count, rectangle_solid::faces, rectangle_solid::index_count};
    case SolidPrimitive::cone:
        return {cone_solid::vertices, cone_solid::vertex_count, cone_solid::faces, cone_solid::index_count};
    case SolidPrimitive::cylinder:
        return {cylinder_solid::vertices, cylinder_solid::vertex_count, cylinder_solid::faces, cylinder_solid::index_count};
    case SolidPrimitive::ellipsoid:
        return {ellipsoid_solid::vertices, ellipsoid_solid::vertex_count, ellipsoid_solid::faces, ellipsoid_solid::index_count};
    }
    throw std::invalid_argument("Unknown solid primitive");
}

PrimitiveView primitive(WirePrimitive shape)
{
    using namespace geometry_utils;
    switch (shape) {
    case WirePrimitive::box:
        return {obb::vertices, obb::vertex_count, obb::pairs, 2u * obb::pair_count};
    case WirePrimitive::rectangle:
        return {rectangle::vertices, rectangle::vertex_count, rectangle::pairs, 2u * rectangle::pair_count};
    case WirePrimitive::cone:
        return {cone::vertices, cone::vertex_count, cone::pairs, 2u * cone::pair_count};
    case WirePrimitive::ellipsoid:
        return {ellipsoid::vertices, ellipsoid::vertex_count, ellipsoid::pairs, 2u * ellipsoid::pair_count};
    case WirePrimitive::ellipse:
        return {ellipse::vertices, ellipse::vertex_count, ellipse::pairs, 2u * ellipse::pair_count};
    }
    throw std::invalid_argument("Unknown wire primitive");
}

Mesh make_mesh(SolidPrimitive shape, math::float3 const& scale, math::float4x4 const& transform)
{
    auto const view = primitive(shape);
    Mesh mesh;
    mesh.vertices.reserve(view.vertex_count);
    for (std::size_t i = 0; i < view.vertex_count; ++i) {
        auto const* point = view.positions + i * 3;
        mesh.vertices.push_back(transform.transform_position(
            {point[0] * scale.x, point[1] * scale.y, point[2] * scale.z}));
    }
    mesh.indices.assign(view.indices, view.indices + view.index_count);
    return mesh;
}

// Ported from compat/core/sources/geometry_utils.cpp. See its original attribution.
Mesh make_ring(float inner_radius, float width, std::uint16_t segments)
{
    float const outer_radius = inner_radius + width;
    if (segments < 3 || segments > 32768 || !std::isfinite(outer_radius) ||
        inner_radius <= 0.f || width <= 0.f)
        throw std::invalid_argument("Invalid ring dimensions");

    Mesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(segments) * 2);
    mesh.indices.reserve(static_cast<std::size_t>(segments) * 6);
    for (unsigned i = 0; i < segments; ++i) {
        math::sine_cosine const angle(math::pi_x2 * i / segments);
        mesh.vertices.emplace_back(inner_radius * angle.cosine, 0.f, -inner_radius * angle.sine);
        mesh.vertices.emplace_back(outer_radius * angle.cosine, 0.f, -outer_radius * angle.sine);
        auto const current = static_cast<std::uint16_t>(2 * i);
        auto const next = static_cast<std::uint16_t>(2 * ((i + 1) % segments));
        auto const current_outer = static_cast<std::uint16_t>(current + 1);
        auto const next_outer = static_cast<std::uint16_t>(next + 1);
        mesh.indices.insert(mesh.indices.end(), {current, current_outer, next_outer,
            current, next_outer, next});
    }
    return mesh;
}

// The compat torus used 16-bit loop counters and ignored its transform.
Mesh make_torus(float major_radius, float minor_radius, std::uint16_t major_segments,
    std::uint16_t minor_segments, math::float4x4 const& transform)
{
    auto const count = static_cast<std::size_t>(major_segments) * minor_segments;
    if (major_segments < 3 || minor_segments < 3 || count > 65536 ||
        !std::isfinite(major_radius + minor_radius) || minor_radius <= 0.f ||
        major_radius <= minor_radius)
        throw std::invalid_argument("Invalid torus dimensions");

    Mesh mesh;
    mesh.vertices.reserve(count);
    mesh.indices.reserve(count * 6);
    for (unsigned major = 0; major < major_segments; ++major) {
        math::sine_cosine const azimuth(math::pi_x2 * major / major_segments);
        auto const row = major * minor_segments;
        auto const next_row = ((major + 1) % major_segments) * minor_segments;
        for (unsigned minor = 0; minor < minor_segments; ++minor) {
            math::sine_cosine const tube(math::pi_x2 * minor / minor_segments);
            float const radius = major_radius + minor_radius * tube.cosine;
            mesh.vertices.push_back(transform.transform_position(
                {radius * azimuth.cosine, minor_radius * tube.sine, radius * azimuth.sine}));

            auto const next_minor = (minor + 1) % minor_segments;
            auto const a = static_cast<std::uint16_t>(row + minor);
            auto const b = static_cast<std::uint16_t>(row + next_minor);
            auto const c = static_cast<std::uint16_t>(next_row + minor);
            auto const d = static_cast<std::uint16_t>(next_row + next_minor);
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
    }
    return mesh;
}

} // namespace northstarcpp::geometry
