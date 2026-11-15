// NorthStarCpp 2026 — first-party material asset authoring.
// A scene actor references a material asset; it never owns a texture path.
#pragma once
#include <QJsonObject>
#include <QImage>
#include <QString>
#include <QStringList>
#include <filesystem>

namespace northstarcpp::editor::material_authoring {
bool is_material(QString const& path);
bool is_texture(QString const& path);
QStringList list_materials(std::filesystem::path const& content_root);
QStringList list_material_functions(std::filesystem::path const& content_root);
// Read a bounded, project-local Material Function through checked VFS paths.
std::string material_function_source(
    std::filesystem::path const& content_root,std::string const& reference);
QStringList list_textures(std::filesystem::path const& content_root);
// Throws on traversal, missing project file, symlink/reparse-point references.
std::filesystem::path resolve_existing(
    std::filesystem::path const& content_root, QString const& reference);
QJsonObject load_descriptor(std::filesystem::path const& content_root,
                            QString const& reference);
void save_descriptor(std::filesystem::path const& content_root,
                     QString const& reference,QJsonObject const& descriptor);
// Generates a new project material (never overwrites) and returns its VFS path.
QString make_material_for_texture(std::filesystem::path const& content_root,
                                  QString const& texture_reference);
// Physically-based studio sphere from the engine's existing material
// preview renderer; safe to compute on a QtConcurrent worker thread.
QImage sphere_preview(std::filesystem::path const& content_root,
                      QString const& material_reference,int dimension=96);
// Neutral fallback sphere while asynchronously decoding PBR resources.
QImage neutral_sphere_preview(int dimension=96);
} // namespace northstarcpp::editor::material_authoring
