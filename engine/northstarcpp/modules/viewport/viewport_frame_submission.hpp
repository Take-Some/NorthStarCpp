#pragma once
// Stateless render-provider submission: host-owned buffers, no ABI changes.
#include <northstarcpp/render_pick_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_shader_reload_api.h>
#include <cstdint>
#include <vector>

namespace northstarcpp::viewport_detail {
bool submit_pick_streams(void* renderer,NorthStarRenderPickApiV2 const* api,
                         bool collect,bool complete,
                         std::vector<NorthStarRenderPickVertex> const& triangles,
                         std::vector<std::uint64_t> const& cubes);

enum class MaterialSubmitStatus {
    ok, missing_graph_provider, graph_upload_failed,
    material_upload_failed, shader_references_failed
};

MaterialSubmitStatus submit_material_streams(
    void* renderer,
    NorthStarRenderMaterialsApi const* materials,
    NorthStarRenderMaterialsApiV2 const* materials_v2,
    NorthStarRenderShaderReloadApi const* reload,
    NorthStarRenderShaderReloadApiV2 const* reload_v2,
    bool requires_v2,
    std::vector<NorthStarMaterialBatch> const& batches,
    std::vector<NorthStarMaterialBatchV2> const& batches_v2,
    std::vector<const char*> const& keys);
} // namespace northstarcpp::viewport_detail
