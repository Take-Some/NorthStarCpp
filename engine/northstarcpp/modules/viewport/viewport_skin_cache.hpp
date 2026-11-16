#pragma once

#include <northstarcpp/native_model.hpp>
#include <northstarcpp/render_skinning_api.h>
#include <filesystem>
#include <optional>
#include <string_view>

namespace northstarcpp::viewport_detail {

using PreparedSkinSections = std::vector<std::vector<NorthStarSkinMaterialVertex>>;

std::filesystem::path skin_cache_directory();

struct SkinSectionCacheRequest {
    MeshAsset const& mesh;
    std::vector<SkinWeights> const& weights;
    std::vector<std::uint8_t> const* arm_mask;
    std::string_view provider;
    std::string_view provider_version;
};

struct PreparedSkinResult {
    PreparedSkinSections sections;
    bool cache_hit=false;
};

// Testable with an isolated directory. A failed cache read or write leaves
// the native, fully detailed section preparation available.
PreparedSkinResult prepare_skin_sections(SkinSectionCacheRequest const& request,
                                         std::filesystem::path const& cache_dir);

} // namespace northstarcpp::viewport_detail
