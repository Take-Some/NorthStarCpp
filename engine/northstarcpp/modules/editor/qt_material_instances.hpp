// First-class project material instance authoring. Inherits runtime schema 1.
#pragma once
#include <QJsonObject>
#include <QString>
#include <filesystem>
class QWidget;

namespace northstarcpp::editor::material_authoring {
bool is_material_instance(QString const& reference);
bool edit_material_instance_dialog(QWidget* owner,
                                  std::filesystem::path const& content_root,
                                  QString const& reference);
// Accepts project-owned descriptors or material instances as parents.
QString create_material_instance(std::filesystem::path const& content_root,
                                 QString const& parent,QString const& name);
QJsonObject load_material_instance(std::filesystem::path const& content_root,
                                   QString const& reference);
void save_material_instance(std::filesystem::path const& content_root,
                            QString const& reference,QJsonObject const& document);
// Returns effective settings for the current editor's familiar Surface controls.
QJsonObject resolve_material_instance(std::filesystem::path const& content_root,
                                      QString const& reference);
}
