#pragma once
#include <filesystem>
#include <string>
#include <QJsonObject>
class QWidget;
namespace northstarcpp::editor {
// Shared strict source validator used by JSON and visual graph modes.
void validateMaterialFunctionDocument(QJsonObject const& function,
    std::filesystem::path const& root,std::string const& reference);
int openMaterialFunctionEditor(QWidget* parent,
    std::filesystem::path const& contentRoot,
    std::string const& reference);
}
