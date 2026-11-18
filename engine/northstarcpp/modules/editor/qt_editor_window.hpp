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
#include <array>
#include <optional>
#include <unordered_set>
#include <cstddef>
class QDockWidget;
class QGroupBox;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QComboBox;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QPlainTextEdit;
class QAction;
class QMenu;
class QProcess;
class QDialog;
namespace northstarcpp::editor {
class QtContentBrowser;
class QtEditorViewport;
class QtPlaySurface;
class QtPlaceActorsPanel;
class QtTextAssetWorkspace;
class QtEditorWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit QtEditorWindow(QWidget* parent=nullptr);
    bool openScene(QString const& file);
    bool saveScene(QString file={});
    // Store a real viewport GPU frame beside the project's manifest.
    bool captureProjectThumbnail(QString* diagnostic=nullptr);
    bool runSmoke(QString const& root);
    bool runPlaceActorsSmoke();
    bool runGpuSmoke(QString const& root);
    bool runVisualPrefabGpuSmoke(); // real native mesh, mouse ID picking, DPI
    bool runPlayerCheckSmoke(QString const& root);
    QString playerExecutable() const;
    void playGame();
    void playGameStandalone();
    void stopGame();
    void pauseGame();
    void resumeGame();
    void stepGame();
    void ejectGame();
    void possessGame();
    void updatePieControls();
    bool sendPieCommand(wchar_t const* command);
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
    std::unordered_set<std::uint64_t> selectedActorIds_;
    // Explicit per-property user edits; never infer batch dirtiness from
    // the primary actor when selected objects contain mixed values.
    std::unordered_set<QDoubleSpinBox*> inspectorDirtyScalars_;
    std::unordered_set<QCheckBox*> inspectorDirtyChecks_;
    bool dirty_=false, uiSync_=false;
    std::optional<Scene> gizmoBefore_;
    bool gizmoChanged_=false;
    float gizmoRawTotal_=0.f, gizmoAppliedTotal_=0.f;
    QElapsedTimer gizmoInspectorTimer_;
    QtEditorViewport* viewport_{};
    QtContentBrowser* browser_{};

    class QtContentDrawer* contentDrawer_{};
    QTreeWidget* outliner_{};
    std::uint64_t outlinerContentHash_=0;
    bool outlinerContentValid_=false;
    QHash<quint64,QTreeWidgetItem*> outlinerItemsById_;
    QTreeWidget* dependencies_{};
    QTreeWidget* providers_{};
    QTreeWidget* components_{};
    QLineEdit* outlinerSearch_{};
    QLineEdit* objectName_{};
    QLineEdit* assetPath_{};
    QLineEdit* visualPrefabPath_{};
    QPushButton* visualPrefabApply_{};
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
    QGroupBox* lightSettings_{};
    QComboBox* lightType_{};
    QDoubleSpinBox *lightIntensity_{},*lightRange_{},*lightCone_{};
    QDoubleSpinBox *lightR_{},*lightG_{},*lightB_{};
    QDoubleSpinBox *lightDirX_{},*lightDirY_{},*lightDirZ_{};
    QPushButton* lightApply_{};
    QComboBox* colliderShape_{};
    QComboBox* colliderObjectType_{};
    QSpinBox* colliderSplits_{};
    QComboBox* colliderHullPreview_{};
    bool collisionHullPreviewForcedProxy_=false;
    std::array<QComboBox*,16> colliderResponses_{};
    QComboBox* colliderPreset_{};
    QPushButton* colliderSavePreset_{};
    QPushButton* colliderEditChannels_{};
    QPushButton* colliderGenerate_{};
    QDoubleSpinBox *colliderX_{},*colliderY_{},*colliderZ_{};
    QDoubleSpinBox *colliderOffsetX_{},*colliderOffsetY_{},*colliderOffsetZ_{};
    QPlainTextEdit* messages_{};
    QDockWidget *outlinerDock_{},*detailsDock_{},*contentDock_{},*dependencyDock_{},*providerDock_{},*placeActorsDock_{};
    QtPlaceActorsPanel* placeActors_{};
    QAction *undoAction_{},*redoAction_{},*saveAction_{};
    QAction *playAction_{},*stopAction_{};
    QAction *pauseAction_{},*resumeAction_{},*stepAction_{},*ejectAction_{};
    QProcess* playerProcess_{};
    QtPlaySurface* playSurface_{};
    QByteArray immersiveWorkspaceBefore_{};
    bool immersiveViewport_=false;
    QByteArray playWorkspaceBefore_{};
    bool playMenuWasVisible_=false;
    bool playStatusWasVisible_=false;
    bool embeddedPlay_=false;
    bool piePaused_=false;
    bool pieEjected_=false;
    QString configuredPlayerPath_;
    QAction* exploreModeAction_{};
    QAction* fpsModeAction_{};
    QLineEdit* detailsSearch_{};
    std::function<void()> updateDetailsFilter_;

    void wireAssetMutation(QtContentBrowser* browser);
    QtContentBrowser* createContentBrowserTab(QString const& dockId,
                                               bool activate=true);
    void choosePlayerExecutable();
    void launchGame(bool embedded);
    void hideEditorChromeForPlay();
    void restoreEditorChromeAfterPlay(bool clearSnapshot);
    QtPlaySurface* beginPlayOverlay();
    void endPlayOverlay();
    void applyDefaultEditorLayout();
    void applyAssetEditorLayout();
    void toggleImmersiveViewport();
    void setupUi();
    void setupOutlinerPanel(std::function<QMenu*(QWidget*)> const& makePlaceActorMenu);
    void setupInspectorAndWorkspace(QMenu* viewMenu,QMenu* assetMenu,QAction* configureCharacter);
    void wireWorkspaceSignals(QPushButton* apply);
    void updateAll();
    void updateOutliner();
    void updateInspector();
    void updateComponentInspector();
    void editComponent(bool create);
    void deleteComponent();
    void updateDependencies();
    void refreshProviderInspector();
    void chooseObject(int index,bool preserveMulti=false);
    std::uint64_t selectedPlayerBlueprintPawn() const;
    void editSelectedPlayerBlueprint();
    void renameSelected();
    void duplicateSelected();
    void setSelectedVisibility(bool visible);
    void moveSelectedToLayer(QString const& layer);
    void selectChildren();
    void deleteSelected();
    void editScene(std::function<void(Scene&)> change,int selectedAfter=-2);
    void createActor(QString const& asset,QVector3D position);
    void placeActorById(QString const& id,QVector3D position);
    void instantiatePrefab(QString const& reference,QVector3D position);
    void createProceduralActor(QString const& title,
                               NorthStarGeometryRequest request,
                               QVector3D position={0,0,0});
    void createMaterial();
    void refreshMaterialChoices(bool force=false);
    void queueMaterialPreviews(int nearIndex=1);
    void assignMaterial(int actor,QString const& reference);
    void openMaterialEditor(QString const& reference);
    void openTextAsset(QString const& physical,bool sharedAsset);
    void openUiAsset(QString const& physical,bool sharedAsset);
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
