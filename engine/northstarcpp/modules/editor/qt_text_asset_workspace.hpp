#pragma once
#include <QDockWidget>
#include <QString>
#include <QByteArray>
class QTabWidget;
class QWidget;
namespace northstarcpp::editor {
class QtTextAssetWorkspace final : public QDockWidget {
    Q_OBJECT
public:
    explicit QtTextAssetWorkspace(QWidget* parent=nullptr);
    bool openFile(QString const& path,bool sharedAsset);
    bool confirmCloseAll();
    int documentCount() const;
    QTabWidget* tabs() const noexcept { return tabs_; }
signals:
    // A shader is announced only after glslc successfully validated saved source.
    // Consumer must rebuild/invalidate the corresponding provider pipeline.
    void assetSaved(QString absolutePath,bool validatedShader);
    void shaderCompiled(QString absolutePath,QByteArray spirv);
private:
    bool closeDocument(int index);
    QTabWidget* tabs_{};
};
}
