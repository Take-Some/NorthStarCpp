// NorthStar unified Qt editor shell, dockable panels and scene authoring.
#pragma once
#include "editor_tools.hpp"
#include <northstarcpp/scene.hpp>
#include <northstarcpp/geometry_plugin.h>
#include <QMainWindow>
#include <QElapsedTimer>
#include <QVector3D>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QImage>
#include <filesystem>
#include <functional>
#include <optional>
#include <cstddef>
class QDockWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QComboBox;
class QPushButton;
class QDoubleSpinBox;
class QCheckBox;
class QPlainTextEdit;
class QAction;
class QProcess;
class QDialog;
namespace northstarcpp::editor {
class QtContentBrowser;
class QtEditorViewport;
class QtPlaySurface;
class QtEditorWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit QtEditorWindow(QWidget* parent=nullptr);
    bool openScene(QString const& file);
    bool saveScene(QString file={});
    bool runSmoke(QString const& root);
    bool runGpuSmoke(QString const& root);
    bool runPlayerCheckSmoke(QString const& root);
    QString playerExecutable() const;
    void playGame();
    void playGameStandalone();
    void stopGame();
    bool runEmbeddedPlaySmoke(QString const& playerExecutable);
    bool runEmbeddedRenderSmoke(QString const& playerExecutable,
                                QString const& scene);
    int objectCount() const noexcept {return static_cast<int>(scene_.objects.size());}
    QtContentBrowser* contentBrowser() const {return browser_;}
    // Editor-owned Content Browser: no external process or second window.
    void showContentBrowser(QString const& virtualReference={});
protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    Scene scene_;
    std::filesystem::path file_;
    editor_tools::History history_;
    editor_tools::GizmoOptions gizmoOptions_;
    int selection_=-1;
    bool dirty_=false, uiSync_=false;
    std::optional<Scene> gizmoBefore_;
    bool gizmoChanged_=false;
    float gizmoRawTotal_=0.f, gizmoAppliedTotal_=0.f;
    QElapsedTimer gizmoInspectorTimer_;
    QtEditorViewport* viewport_{};
    QtContentBrowser* browser_{};
    QTreeWidget* outliner_{};
    QTreeWidget* dependencies_{};
    QTreeWidget* providers_{};
    QTreeWidget* components_{};
    QLineEdit* outlinerSearch_{};
    QLineEdit* objectName_{};
    QLineEdit* assetPath_{};
    QComboBox* materialPicker_{};
    QPushButton* materialEditButton_{};
    QStringList cachedMaterials_;
    QString cachedMaterialRoot_;
    QHash<QString,QImage> materialSpheres_;
    bool materialPreviewBusy_=false;
    std::uint64_t materialPreviewRevision_=0;
    int materialPreviewFocus_=1;
    QDoubleSpinBox *px_{},*py_{},*pz_{},*size_{},*mass_{};
    QDoubleSpinBox *rx_{},*ry_{},*rz_{},*sx_{},*sy_{},*sz_{};
    QCheckBox *visible_{},*collision_{};
    QPlainTextEdit* messages_{};
    QDockWidget *outlinerDock_{},*detailsDock_{},*contentDock_{},*dependencyDock_{},*providerDock_{};
    QAction *undoAction_{},*redoAction_{},*saveAction_{};
    QAction *playAction_{},*stopAction_{};
    QProcess* playerProcess_{};
    QtPlaySurface* playSurface_{};
    QByteArray playWorkspaceBefore_{};
    bool playMenuWasVisible_=false;
    bool playStatusWasVisible_=false;
    bool embeddedPlay_=false;
    QString configuredPlayerPath_;
    QAction* exploreModeAction_{};
    QAction* fpsModeAction_{};

    void choosePlayerExecutable();
    void launchGame(bool embedded);
    QtPlaySurface* beginPlayOverlay();
    void endPlayOverlay();
    void setupUi();
    void updateAll();
    void updateOutliner();
    void updateInspector();
    void updateComponentInspector();
    void editComponent(bool create);
    void deleteComponent();
    void updateDependencies();
    void refreshProviderInspector();
    void chooseObject(int index);
    void renameSelected();
    void duplicateSelected();
    void deleteSelected();
    void editScene(std::function<void(Scene&)> change,int selectedAfter=-2);
    void createActor(QString const& asset,QVector3D position);
    void instantiatePrefab(QString const& reference,QVector3D position);
    void createProceduralActor(QString const& title,
                               NorthStarGeometryRequest request,
                               QVector3D position={0,0,0});
    void createMaterial();
    void refreshMaterialChoices(bool force=false);
    void queueMaterialPreviews(int nearIndex=1);
    void assignMaterial(int actor,QString const& reference);
    void openMaterialEditor(QString const& reference);
    void handleAssetDrop(QString const& reference,QVector3D position,int hitActor);
    void undo();
    void redo();
    void log(QString const& line);
    bool confirmDiscard();
    void restoreWorkspace();
    void persistWorkspace() const;
    QString sceneTitle() const;
};
} // namespace northstarcpp::editor
