#pragma once
#include <QJsonObject>
#include <QString>
#include <filesystem>
namespace northstarcpp::editor::material_library_edit {
QJsonObject load(std::filesystem::path const& root,QString const& reference);
void save(std::filesystem::path const& root,QString const& reference,
          QJsonObject const& edited);
}
