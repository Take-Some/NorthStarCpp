#pragma once
#include <QString>
#include <QByteArray>
#include <functional>
class QWidget;
class QPlainTextEdit;
namespace northstarcpp::editor::text_assets {
enum class Format {Unsupported, Markdown, JavaScript, VertexShader, FragmentShader,
                   TypeScript, Json, Plain};
// Shared Qt source surface: line numbers, syntax colors, current-line focus,
// and subtle horizontal ruling for all logical lines (including read-only).
QPlainTextEdit* createSourceEditor(QWidget* parent,Format format,bool readOnly=false);
Format formatFor(QString const& filename) noexcept;
bool isEditable(Format format) noexcept;
void open(QWidget* parent,QString const& absolutePath,bool sharedAsset);
// The same editor implementation can be embedded in Qt Editor document tabs.
QWidget* createEmbedded(QWidget* parent,QString const& absolutePath,bool sharedAsset,
                        std::function<void(QString const&,bool)> onSaved,
                        std::function<void(QString const&,QByteArray const&)> onCompiled={});
bool confirmClose(QWidget* embedded);
bool modified(QWidget* embedded);
QString filePath(QWidget* embedded);
} 
