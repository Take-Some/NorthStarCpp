// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/collision_queries.hpp>
#include <northstarcpp/geometry.hpp>
#include <northstarcpp/compat/core/math_standalone.h>

#include <limits>
#include <stdexcept>

static_assert(NORTHSTARCPP_PLATFORM_WINDOWS);
static_assert(NORTHSTARCPP_PLATFORM_WINDOWS_64 == (sizeof(void*) == 8));

int main()
{
    using namespace northstarcpp::math;

    float3 const small(1.e-20f, -2.e-20f, 3.e-20f);
    if (!similar(normalize_precise(small), normalize(float3(1.f, -2.f, 3.f))))
        return 1;

    float3 up, right;
    float3 const direction(1.f, 0.f, 0.f);
    generate_orthonormal_basis(direction, up, right);
    if (!similar(up.magnitude(), 1.f) || !similar(right.magnitude(), 1.f)
        || !is_zero(dot_product(up, right)) || !is_zero(dot_product(direction, right)))
        return 2;

    auto const translation = create_translation(float3(1.f, 2.f, 3.f));
    auto const identity = mul4x3(translation, invert4x3(translation));
    if (!similar(float3(identity.c.x, identity.c.y, identity.c.z), float3(0.f, 0.f, 0.f)))
        return 3;

    rectangle<float2_pod> const bounds(float2(1.f, 2.f), float2(4.f, 6.f));
    if (!similar(bounds.volume(), 12.f) || !similar(table_sin(90), 1.f))
        return 4;

    using namespace northstarcpp::geometry;
    auto valid_indices = [](PrimitiveView const& view, std::size_t arity) {
        if (!view.vertex_count || !view.index_count || view.index_count % arity)
            return false;
        for (std::size_t i = 0; i < view.index_count; ++i)
            if (view.indices[i] >= view.vertex_count)
                return false;
        return true;
    };
    for (auto shape : {SolidPrimitive::cube, SolidPrimitive::rectangle, SolidPrimitive::cone,
                       SolidPrimitive::cylinder, SolidPrimitive::ellipsoid})
        if (!valid_indices(primitive(shape), 3))
            return 5;
    for (auto shape : {WirePrimitive::box, WirePrimitive::rectangle, WirePrimitive::cone,
                       WirePrimitive::ellipsoid, WirePrimitive::ellipse})
        if (!valid_indices(primitive(shape), 2))
            return 6;

    auto const cube = make_mesh(SolidPrimitive::cube, {2.f, 3.f, 4.f}, translation);
    if (cube.vertices.size() != 8 || cube.indices.size() != 36 ||
        !similar(cube.vertices.front(), float3(-1.f, 5.f, -1.f)))
        return 7;
    auto const ring = make_ring(2.f, 1.f, 8);
    if (ring.vertices.size() != 16 || ring.indices.size() != 48 ||
        !similar(ring.vertices[1], float3(3.f, 0.f, 0.f)) ||
        ring.indices.back() != 0)
        return 8;

    auto const torus = make_torus(3.f, 1.f, 256, 256, translation);
    if (torus.vertices.size() != 65536 || torus.indices.size() != 6 * 65536 ||
        !similar(torus.vertices.front(), float3(5.f, 2.f, 3.f)) ||
        torus.indices[torus.indices.size() - 2] != 0 || torus.indices.back() != 255)
        return 9;
    try {
        make_torus(3.f, 1.f, 257, 256, translation);
        return 10;
    } catch (std::invalid_argument const&) {
    }

    aabb box;
    box.min = {-1.f, -1.f, 4.f};
    box.max = {1.f, 1.f, 6.f};
    using northstarcpp::collision::ray_box_entry;
    auto const hit = ray_box_entry(box, {1.f, 0.f, 0.f}, {0.f, 0.f, 2.f}, 2.f);
    auto const reverse = ray_box_entry(box, {0.f, 0.f, 7.f}, {0.f, 0.f, -1.f}, 2.f);
    auto const inside = ray_box_entry(box, {0.f, 0.f, 5.f}, {1.f, 0.f, 0.f}, 0.f);
    if (!hit || *hit != 2.f || !reverse || *reverse != 1.f || !inside || *inside != 0.f)
        return 11;
    if (ray_box_entry(box, {2.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 10.f) ||
        ray_box_entry(box, {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 3.f) ||
        ray_box_entry(box, {0.f, 0.f, 5.f}, {0.f, 0.f, 0.f}, 10.f) ||
        ray_box_entry(box, {0.f, 0.f, 0.f}, {0.f, 0.f, std::numeric_limits<float>::quiet_NaN()}, 10.f))
        return 12;

    float4x4 identity_matrix;
    identity_matrix.identity();
    frustum const direct3d(identity_matrix, clip_depth::zero_to_one);
    frustum const legacy(identity_matrix);
    box.min = {-0.5f, -0.5f, -0.75f};
    box.max = {0.5f, 0.5f, -0.25f};
    if (direct3d.test_inexact(box) != intersection_outside ||
        legacy.test_inexact(box) != intersection_inside)
        return 13;
    box.max.z = 0.f;
    if (direct3d.test_inexact(box) != intersection_intersect)
        return 14;

    auto const projection = create_projection(pi_d4, 1.f, 0.1f, 250.f);
    frustum const perspective(projection, clip_depth::zero_to_one);
    box.identity();
    box.move({0.f, 0.f, 5.f});
    if (perspective.test_inexact(box) != intersection_inside)
        return 15;
    box.move({8.f, 0.f, 0.f});
    if (perspective.test_inexact(box) != intersection_outside)
        return 16;
    box.move({-8.f, 0.f, 250.f});
    if (perspective.test_inexact(box) != intersection_outside)
        return 17;

    return 0;
}
