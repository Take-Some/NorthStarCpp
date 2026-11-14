// Read-only material assets and bound textures for Qt previews.
// Validated NEF8 parsers, bounded physical reads, no dynamic engine.assets.
#pragma once
#include <northstarcpp/shared_assets.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace northstarcpp::editor {
struct MaterialPreviewSource {
    std::optional<shared_assets::Material> material;
    std::vector<shared_assets::ResolvedAsset<shared_assets::Texture>> textures;
    std::vector<std::string> missing;
};
MaterialPreviewSource loadMaterialPreviewSource(
    std::filesystem::path const& root,std::string const& reference);
}
