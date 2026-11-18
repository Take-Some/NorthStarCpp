#pragma once
// NorthStar per-Viewport CPU frame submission scratch. No provider ABI changes.
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_pick_api.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace northstarcpp::viewport_detail {
struct ViewportFrameScratch {
    using MaterialDraw = std::pair<std::string, std::vector<NorthStarMaterialVertex>>;
    std::vector<NorthStarRenderPickVertex> pick_triangles;
    std::vector<std::uint64_t> pick_cube_entities;
    std::vector<MaterialDraw> material_draws;
    struct MaterialSlot { std::size_t index=0; std::uint64_t frame=0; };
    // Remember lookup nodes across frames rather than allocating them anew.
    std::unordered_map<std::string, MaterialSlot> material_indices;
    std::uint64_t frame_epoch=0;
    std::size_t active_material_draws=0;
    std::vector<NorthStarMaterialBatch> gpu_materials;
    std::vector<NorthStarMaterialBatchV2> gpu_materials_v2;
    // Three reusable, provider-neutral sprite batches for transient VFX.
    // GPU providers retain neither the pointer nor the sprites after submit.
    std::array<std::vector<NorthStarMaterialVertex>,3> weapon_fx_vertices;
    // These pointers refer to stable material_draws names until submit finishes.
    std::vector<const char*> material_keys;

    void begin_frame() {
        pick_triangles.clear();
        pick_cube_entities.clear();
        for(auto& entry:material_draws)
            entry.second.clear();
        // Bound long-running, dynamically authored material-name caches without
        // limiting rendered material count or geometry detail.
        if(++frame_epoch==0 || material_indices.size()>8192u) {
            material_indices.clear();
            frame_epoch=1;
        }
        active_material_draws=0;
        gpu_materials.clear();
        gpu_materials_v2.clear();
        for(auto& vertices:weapon_fx_vertices)vertices.clear();
        material_keys.clear();
    }

    // Release project-local keys and oversized vertex buffers on a VFS remount.
    void reset_project() {
        begin_frame();
        material_draws.clear();
        material_indices.clear();
    }

    std::vector<NorthStarMaterialVertex>& material_vertices(
        std::string const& reference) {
        auto found=material_indices.find(reference);
        if(found!=material_indices.end() && found->second.frame==frame_epoch)
            return material_draws[found->second.index].second;
        if(active_material_draws>=NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES)
            throw std::runtime_error("Too many distinct material batches");
        std::size_t const index=active_material_draws;
        if(index<material_draws.size()) {
            material_draws[index].first=reference;
            material_draws[index].second.clear();
        } else {
            material_draws.emplace_back(reference,std::vector<NorthStarMaterialVertex>{});
        }
        if(found==material_indices.end())
            material_indices.emplace(material_draws[index].first,
                                     MaterialSlot{index,frame_epoch});
        else
            found->second={index,frame_epoch};
        ++active_material_draws;
        return material_draws[index].second;
    }
};
} // namespace northstarcpp::viewport_detail
