#pragma once
#include <QString>
class QWidget;
namespace northstarcpp::editor {
// Open a .nui document in an independent modal editor.
// Shared engine files are read-only; project files use atomic saving.
bool editUiAsset(QWidget* parent,QString const& physicalFile,bool sharedAsset);
}
