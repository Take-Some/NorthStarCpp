#include "qt_editor_window.hpp"
#include "qt_content_browser.hpp"
#include "qt_content_drawer.hpp"
#include "qt_text_asset_workspace.hpp"
#include "qt_material_authoring.hpp"
#include <QDockWidget>
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
void QtEditorWindow::openTextAsset(QString const& physical,bool sharedAsset) {
    if(contentDrawer_)contentDrawer_->hide();
    if(!textWorkspace_ || !textWorkspace_->openFile(physical,sharedAsset))
        QMessageBox::warning(this,"Code / Markdown",
            "Cannot open source document: "+physical);
}
} // namespace northstarcpp::editor
