// Native, modeless Camera Studio for the current Qt Editor.
// Uses the same engine-owned camera preset schema as Player VFS.
#pragma once
#include <filesystem>
class QWidget;
namespace northstarcpp::editor {
class QtEditorViewport;
void openCameraStudio(QWidget* parent, QtEditorViewport* viewport,
                      std::filesystem::path const& project_assets);
} // namespace northstarcpp::editor
