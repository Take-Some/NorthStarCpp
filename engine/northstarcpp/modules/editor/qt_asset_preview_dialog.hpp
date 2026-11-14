// NorthStarCpp: Modal preview of a selected physical or NEF8 ListFile asset.
#pragma once
#include <northstarcpp/editor_asset_catalog.hpp>
#include <filesystem>
#include <string>
class QWidget;
namespace northstarcpp::editor {
// QDialog::exec() returns Accepted/Rejected; source asset remains read-only.
int openAssetPreviewModal(QWidget* parent,
                          std::filesystem::path const& content_root,
                          std::string const& asset_reference,AssetKind kind);
}
