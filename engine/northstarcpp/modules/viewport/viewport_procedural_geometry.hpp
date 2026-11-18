#pragma once
// Pure CPU procedural geometry stage. Vertex order and UV policy preserved.
#include <northstarcpp/geometry_service.hpp>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_pick_api.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/scene.hpp>
#include <cstdint>
#include <vector>
namespace northstarcpp::viewport_detail {
void append_procedural_geometry(
    std::vector<NorthStarMaterialVertex>& vertices,
    GeometryData const& geometry,
    SceneObject const& object,
    NorthStarRenderCube const& cube,
    std::vector<NorthStarRenderPickVertex>& pick_vertices,
    bool collect_pick,std::uint64_t entity_id,bool& pick_complete);
}
