#pragma once
#include <filesystem>
#include <string>
class QWidget;
namespace northstarcpp::editor {
int openMaterialFunctionEditor(QWidget* parent,
    std::filesystem::path const& contentRoot,
    std::string const& reference);
}
