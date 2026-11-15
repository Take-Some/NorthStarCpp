// NorthStar Material GPU preview — reuses the active engine.render provider.
#pragma once
#include <northstarcpp/scene.hpp>
#include <QJsonObject>
#include <QHash>
#include <QByteArray>
#include <QTemporaryDir>
#include <QWidget>
#include <filesystem>

class QLabel;
class QTimer;
class QComboBox;
class QCheckBox;
class QFileSystemWatcher;

namespace northstarcpp::editor {
class QtEditorViewport;

class MaterialGpuPreview final : public QWidget {
    Q_OBJECT
public:
    explicit MaterialGpuPreview(std::filesystem::path contentRoot,
                                QWidget* parent=nullptr);
    void start();
    void setMaterial(QJsonObject descriptor);
    bool gpuActive() const;
    QString diagnostic() const;
    // Deterministic QA path: material is validated and staged but not rendered.
    bool stageForTest(QJsonObject descriptor,QString* reason=nullptr);
signals:
    void shaderDiagnostic(QString message);
private:
    bool stage(QJsonObject const& descriptor,QString* reason);
    void updatePreview();
    void refreshFunctionWatchers();
    void applyPreflight(QJsonObject const& descriptor,QString const& error);
    void setMesh(int index);
    void setGrid(bool enabled);
    std::filesystem::path contentRoot_;
    QTemporaryDir scratch_;
    northstarcpp::Scene scene_;
    QtEditorViewport* viewport_{};
    QTimer* throttle_{};
    QFileSystemWatcher* functionWatcher_{};
    QLabel* status_{};
    QComboBox* mesh_{};
    QCheckBox* grid_{};
    QJsonObject pending_;
    bool started_=false;
    quint64 revision_=0;
    QHash<QByteArray,QString> preflightCache_;
};
} // namespace northstarcpp::editor
