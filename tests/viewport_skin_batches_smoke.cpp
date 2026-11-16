#include "../engine/northstarcpp/modules/viewport/viewport_material_batches.hpp"

#include <stdexcept>

#define REQUIRE(condition) do { if (!(condition)) \
    throw std::runtime_error("Viewport skin batch smoke failed: " #condition); } while (false)

int main()
{
    using namespace northstarcpp;
    viewport_detail::SkinFrameBatches frame;
    MaterialDefinition material;
    material.alpha_cutoff = 0.25f;
    material.textures[0] = {1, 1, {1, 2, 3, 255}};
    std::vector<NorthStarSkinMaterialVertex> const vertices(3);

    math::float4x4 joint, world;
    joint.identity();
    joint.elements[3][0] = 2.f;
    world.identity();
    world.elements[0][0] = 3.f;
    world.elements[3][0] = 5.f;

    frame.begin_frame();
    auto const first = frame.append_palette({joint}, world);
    frame.add_draw(material, vertices, first);
    frame.add_draw(material, vertices, first);

    // Force palette storage to grow after registering the first draws.
    auto const later = frame.append_palette(
        std::vector<math::float4x4>(NORTHSTARCPP_SKIN_MAX_JOINTS, joint), joint);
    frame.add_draw(material, vertices, later);
    auto const& batches = frame.build();
    REQUIRE(batches.size() == 3);
    REQUIRE(batches[0].palette == batches[1].palette);
    REQUIRE(batches[0].palette != batches[2].palette);
    REQUIRE(batches[0].palette[12] == 11.f);
    REQUIRE(batches[2].palette[(NORTHSTARCPP_SKIN_MAX_JOINTS - 1) * 16 + 12] == 4.f);
    REQUIRE(batches[0].joint_count == 1);
    REQUIRE(batches[2].joint_count == NORTHSTARCPP_SKIN_MAX_JOINTS);
    REQUIRE(batches[0].vertices == vertices.data());
    REQUIRE(batches[0].vertex_count == vertices.size());
    REQUIRE(batches[0].material.material.textures[0].rgba8 == material.textures[0].rgba8.data());
    REQUIRE(batches[0].material.alpha_cutoff == material.alpha_cutoff);

    frame.begin_frame();
    REQUIRE(frame.draw_count() == 0);
    REQUIRE(frame.build().empty());
    auto const next = frame.append_palette({joint}, joint);
    frame.add_draw(material, vertices, next);
    REQUIRE(frame.build().size() == 1);
    REQUIRE(frame.build()[0].palette[12] == 4.f);
}
