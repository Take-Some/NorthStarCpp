// Native previews shared by the Qt Content Browser thumbnail workers.
#pragma once
#include <northstarcpp/editor_asset_catalog.hpp>
#include <QImage>
#include <filesystem>
#include <string>
namespace northstarcpp::editor::browser_internal {
QImage engineThumbnail(std::filesystem::path const& root,
                       std::string const& reference,AssetKind kind);
}
