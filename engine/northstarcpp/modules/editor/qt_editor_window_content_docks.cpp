#include "qt_editor_window.hpp"
#include "qt_editor_viewport.hpp"
#include "qt_content_browser.hpp"
#include "qt_content_drawer.hpp"
#include "qt_text_asset_workspace.hpp"
#include "qt_ui_asset_editor.hpp"
#include "qt_material_authoring.hpp"
#include <QDockWidget>
#include <QDialog>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QWidget>
#include <QMessageBox>
#include <QStatusBar>

namespace northstarcpp::editor {
QtContentBrowser* QtEditorWindow::createContentBrowserTab(
    QString const& dockId,bool activate) {
    if(!browser_ || !contentDock_ || dockId.isEmpty())return nullptr;
    if(auto* existing=findChild<QDockWidget*>(dockId))
        return qobject_cast<QtContentBrowser*>(existing->widget());
    auto* panel=new QDockWidget(
        QString("Content Browser %1").arg(dockId.mid(19)),this);
    panel->setObjectName(dockId);
    panel->setFeatures(QDockWidget::DockWidgetClosable|
                       QDockWidget::DockWidgetMovable);
    panel->setAllowedAreas(Qt::BottomDockWidgetArea|
                           Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    auto* content=new QtContentBrowser(panel);
    wireAssetMutation(content);
    content->setEditorDockMode(true);
    content->setSharedContentRoot(browser_->sharedContentRoot());
    content->setContentRoot(browser_->contentRoot());
    if(browser_->viewingSharedAssets())content->selectAssetSource(true);
    panel->setWidget(content);
    connect(browser_,&QtContentBrowser::contentRootsChanged,content,
            [content](QString const& project,QString const& shared) {
        content->setSharedContentRoot(shared);
        content->setContentRoot(project);
    });
    connect(content,&QtContentBrowser::assetActivated,this,
            [this](QString const& ref) {
        if(ref.endsWith(".nprefab",Qt::CaseInsensitive))
            instantiatePrefab(ref,{0,0,0});
        else if(material_authoring::is_material(ref))
            openMaterialEditor(ref);
        else log("Selected asset: "+ref);
    });
    connect(content,&QtContentBrowser::materialEditorRequested,this,
            [this](QString const& ref){openMaterialEditor(ref);});
    connect(content,&QtContentBrowser::textEditorRequested,this,
            &QtEditorWindow::openTextAsset);
    connect(content,&QtContentBrowser::uiAssetEditorRequested,this,
            &QtEditorWindow::openUiAsset);
    connect(content,&QtContentBrowser::assetSelected,this,
            [this](QString const& ref) {
        if(!ref.isEmpty())statusBar()->showMessage("Content / "+ref,2500);
    });
    addDockWidget(Qt::BottomDockWidgetArea,panel);
    tabifyDockWidget(contentDock_,panel);
    if(activate) {
        panel->show();
        panel->raise();
    }else panel->hide();
    return content;
}
// A modal document host keeps editing independent of the docked browser.
// QtTextAssetWorkspace still owns tabs, syntax support and dirty tracking.
namespace {
class QtAssetDocumentModal final : public QDialog {
public:
    explicit QtAssetDocumentModal(QWidget* parent):QDialog(parent) {
        setObjectName("assetEditorModal");
        setWindowTitle("NorthStar Asset Editor");
        setWindowModality(Qt::WindowModal);
        resize(1120,760);
        setMinimumSize(720,460);
        auto* layout=new QVBoxLayout(this);
        layout->setContentsMargins(4,4,4,4);
        documents_=new QtTextAssetWorkspace(this);
        documents_->setFeatures(QDockWidget::NoDockWidgetFeatures);
        documents_->setTitleBarWidget(new QWidget(documents_));
        layout->addWidget(documents_);
    }
    QtTextAssetWorkspace* documents() const noexcept {return documents_;}
protected:
    void done(int result) override {
        // Escape, the title-bar close button and dialog completion must all
        // respect unsaved document prompts, including Cancel.
        if(documents_ && !documents_->confirmCloseAll())return;
        QDialog::done(result);
    }
private:
    QtTextAssetWorkspace* documents_{};
};
} // namespace

void QtEditorWindow::openUiAsset(QString const& physical,bool sharedAsset) {
    if(contentDrawer_)contentDrawer_->hide();
    if(editUiAsset(this,physical,sharedAsset) && browser_)
        browser_->refresh();
}
void QtEditorWindow::openTextAsset(QString const& physical,bool sharedAsset) {
    if(contentDrawer_)contentDrawer_->hide();
    QtAssetDocumentModal dialog(this);
    auto* documents=dialog.documents();
    connect(documents,&QtTextAssetWorkspace::assetSaved,this,
            [this](QString const& path,bool shaderValidated) {
        if(shaderValidated)
            log("GLSL compiled; awaiting render provider pipeline: "+path);
        else log("Source asset saved: "+path);
        if(browser_)browser_->refresh();
    });
    connect(documents,&QtTextAssetWorkspace::shaderCompiled,this,
            [this](QString const& path,QByteArray const& spirv) {
        try {
            auto const count=viewport_->queueCompiledShader(path,spirv);
            if(!count)
                log("Shader has no active linked YMAT materials: "+path);
            else log(QString("Shader compile queued for %1 material PSO(s): %2")
                     .arg(count).arg(path));
        }catch(std::exception const& error) {
            log("Shader reload rejected: "+QString::fromUtf8(error.what()));
        }
    });
    if(!documents->openFile(physical,sharedAsset)) {
        QMessageBox::warning(this,"Asset Editor",
            "Cannot open source document: "+physical);
        return;
    }
    dialog.setWindowTitle(QFileInfo(physical).fileName()+" - NorthStar Asset Editor");
    dialog.exec();
}
} // namespace northstarcpp::editor
