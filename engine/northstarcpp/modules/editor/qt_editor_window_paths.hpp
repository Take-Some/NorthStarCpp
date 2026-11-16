// Reusable editor path/camera helpers shared by shell, Play-In-Editor and QA.
#pragma once
#include "editor_camera.hpp"
#include <QString>
#include <QVariant>
#include <filesystem>
#include <string>
namespace northstarcpp::editor {
class QtEditorViewport;
namespace window_detail {
std::string utf8(QString const& value);
QString from8(std::string const& value);
std::filesystem::path native(QString const& value);
QString qpath(std::filesystem::path const& value);
QVariantList cameraValues(editor_tools::EditorCamera const& camera);
bool applyCameraValues(QtEditorViewport* viewport,QVariant const& data);
QString sceneCameraKey(std::filesystem::path const& file);
QString folderRoot(std::filesystem::path const& scene);
bool isMesh(QString const& path);
std::filesystem::path sharedMaterialRoot();
bool availableMaterial(std::filesystem::path const& root,QString const& reference);
}
}
