#include "viewport_frame_submission.hpp"
namespace northstarcpp::viewport_detail {

bool submit_pick_streams(void* renderer,NorthStarRenderPickApiV2 const* api,
                         bool collect,bool complete,
                         std::vector<NorthStarRenderPickVertex> const& triangles,
                         std::vector<std::uint64_t> const& cubes) {
    if(!api)return true;
    bool const valid=collect && complete;
    auto const* vertices=valid && !triangles.empty()?triangles.data():nullptr;
    auto const count=valid?static_cast<std::uint32_t>(triangles.size()):0u;
    auto const* ids=valid && !cubes.empty()?cubes.data():nullptr;
    auto const nc=valid?static_cast<std::uint32_t>(cubes.size()):0u;
    return api->set_world_triangles(renderer,vertices,count) &&
           api->set_cube_entities(renderer,ids,nc);
}

MaterialSubmitStatus submit_material_streams(
    void* renderer,
    NorthStarRenderMaterialsApi const* materials,
    NorthStarRenderMaterialsApiV2 const* materials_v2,
    NorthStarRenderShaderReloadApi const* reload,
    NorthStarRenderShaderReloadApiV2 const* reload_v2,
    bool requires_v2,
    std::vector<NorthStarMaterialBatch> const& batches,
    std::vector<NorthStarMaterialBatchV2> const& batches_v2,
    std::vector<const char*> const& keys) {
    if(requires_v2 && !materials_v2)
        return MaterialSubmitStatus::missing_graph_provider;
    if(materials_v2) {
        if(!materials_v2->set_batches_v2(renderer,
            batches_v2.empty()?nullptr:batches_v2.data(),
            static_cast<std::uint32_t>(batches_v2.size())))
            return MaterialSubmitStatus::graph_upload_failed;
    } else if(materials &&
              !materials->set_batches(renderer,
                batches.empty()?nullptr:batches.data(),
                static_cast<std::uint32_t>(batches.size())))
        return MaterialSubmitStatus::material_upload_failed;

    if(reload_v2 || reload) {
        auto const* refs=keys.empty()?nullptr:keys.data();
        auto const count=static_cast<std::uint32_t>(keys.size());
        if(!(reload_v2 ?
            reload_v2->set_material_references(renderer,refs,count):
            reload->set_material_references(renderer,refs,count)))
            return MaterialSubmitStatus::shader_references_failed;
    }
    return MaterialSubmitStatus::ok;
}
} // namespace northstarcpp::viewport_detail
