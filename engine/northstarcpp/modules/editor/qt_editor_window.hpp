// NorthStar unified Qt editor shell, dockable panels and scene authoring.
#pragma once
#include "editor_tools.hpp"
#include <northstarcpp/scene.hpp>
#include <QMainWindow>
#include <QVector3D>
#include <QString>
#include <filesystem>
#include <functional>
#include <cstddef>
class QDockWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QDoubleSpinBox;
class QCheckBox;
class QPlainTextEdit;
class QAction;
namespace northstarcpp::editor {
class QtContentBrowser;
class QtEditorViewport;
class QtEditorWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit QtEditorWindow(QWidget* parent=nullptr);
    bool openScene(QString const& file);
    bool saveScene(QString file={});
    bool runSmoke(QString const& root);
    int objectCount() const noexcept {return static_cast<int>(scene_.objects.size());}
    QtContentBrowser* contentBrowser() const {return browser_;}
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    Scene scene_;
    std::filesystem::path file_;
    editor_tools::History history_;
    editor_tools::GizmoOptions gizmoOptions_;
    int selection_=-1;
    bool dirty_=false, uiSync_=false;
    QtEditorViewport* viewport_{};
    QtContentBrowser* browser_{};
    QTreeWidget* outliner_{};
    QTreeWidget* dependencies_{};
    QLineEdit* outlinerSearch_{};
    QLineEdit* objectName_{};
    QLineEdit* assetPath_{};
    QDoubleSpinBox *px_{},*py_{},*pz_{},*size_{},*mass_{};
    QDoubleSpinBox *rx_{},*ry_{},*rz_{},*sx_{},*sy_{},*sz_{};
    QCheckBox *visible_{},*collision_{};
    QPlainTextEdit* messages_{};
    QDockWidget *outlinerDock_{},*detailsDock_{},*contentDock_{},*dependencyDock_{};
    QAction *undoAction_{},*redoAction_{},*saveAction_{};
    void setupUi();
    void updateAll();
    void updateOutliner();
    void updateInspector();
    void updateDependencies();
    void chooseObject(int index);
    void editScene(std::function<void(Scene&)> change,int selectedAfter=-2);
    void createActor(QString const& asset,QVector3D position);
    void createMaterial();
    void undo();
    void redo();
    void log(QString const& line);
    bool confirmDiscard();
    void restoreWorkspace();
    void persistWorkspace() const;
    QString sceneTitle() const;
};
} // namespace northstarcpp::editor
