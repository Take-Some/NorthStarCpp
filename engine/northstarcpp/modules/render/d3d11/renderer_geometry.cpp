// Native CPU geometry implementation for d3d11.
#include "renderer_geometry.hpp"
#include <northstarcpp/geometry.hpp>
namespace northstarcpp::render::d3d11::geometry {
void add_cube(std::vector<Vertex>& vertices, northstarcpp::math::float4x4 const& transform,
    std::uint32_t color)
{
    using namespace northstarcpp::geometry;
    static auto const mesh = primitive(SolidPrimitive::cube);
    constexpr float shade[]{0.74f, 1.f, 0.60f, 0.82f, 0.95f, 0.68f};
    float const r = static_cast<float>((color >> 16) & 255) / 255.f;
    float const g = static_cast<float>((color >> 8) & 255) / 255.f;
    float const b = static_cast<float>(color & 255) / 255.f;
    for (std::size_t triangle = 0; triangle < mesh.index_count; triangle += 3) {
        auto const* a = mesh.positions + 3 * mesh.indices[triangle];
        auto const* point_b = mesh.positions + 3 * mesh.indices[triangle + 1];
        auto const* point_c = mesh.positions + 3 * mesh.indices[triangle + 2];
        unsigned face;
        if (a[1] == point_b[1] && a[1] == point_c[1])
            face = a[1] > 0.f ? 1 : 0;
        else if (a[2] == point_b[2] && a[2] == point_c[2])
            face = a[2] > 0.f ? 4 : 2;
        else
            face = a[0] > 0.f ? 3 : 5;
        for (unsigned corner = 0; corner < 3; ++corner) {
            auto const* point = mesh.positions + 3 * mesh.indices[triangle + corner];
            auto const position = northstarcpp::math::float3(point[0], point[1], point[2]) * transform;
            vertices.push_back({position.x, position.y, position.z,
                r * shade[face], g * shade[face], b * shade[face]});
        }
    }
}

} // namespace northstarcpp::render::d3d11::geometry
