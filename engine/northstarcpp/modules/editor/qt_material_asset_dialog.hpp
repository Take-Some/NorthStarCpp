// Reusable standalone Material Editor entry for Content Browser double click.
#pragma once
#include <filesystem>
#include <string>
class QWidget;
namespace northstarcpp::editor {
int openMaterialAssetDialog(QWidget* parent,
    std::filesystem::path const& root,
    std::string const& virtualReference);
} // namespace northstarcpp::editor
