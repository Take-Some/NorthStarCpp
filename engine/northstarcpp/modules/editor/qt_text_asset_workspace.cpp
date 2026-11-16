#include "qt_text_asset_workspace.hpp"
#include "qt_text_asset_editor.hpp"
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QMessageBox>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace northstarcpp::editor {
QtTextAssetWorkspace::QtTextAssetWorkspace(QWidget* parent):QDockWidget("Code / Markdown",parent) {
    setObjectName("textAssetWorkspaceDock");
    setFeatures(DockWidgetClosable|DockWidgetMovable);
    setAllowedAreas(Qt::BottomDockWidgetArea|Qt::RightDockWidgetArea|
                    Qt::LeftDockWidgetArea);
    auto* pane=new QWidget(this);
    auto* layout=new QVBoxLayout(pane);
    layout->setContentsMargins(3,3,3,3);
    tabs_=new QTabWidget(pane);
    tabs_->setObjectName("textDocumentTabs");
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);
    tabs_->setStyleSheet(
        "QTabWidget::pane{border:1px solid #35475a;background:#141b24}"
        "QTabBar::tab{background:#243240;color:#dce6f0;padding:7px 12px}"
        "QTabBar::tab:selected{background:#36516a;color:#ffffff}");
    layout->addWidget(tabs_);
    pane->setLayout(layout);
    setWidget(pane);
    connect(tabs_,&QTabWidget::tabCloseRequested,this,
            [this](int index){closeDocument(index);});
}
int QtTextAssetWorkspace::documentCount() const {return tabs_->count();}
bool QtTextAssetWorkspace::openFile(QString const& path,bool sharedAsset) {
    QFileInfo const info(path);
    if(!info.exists() || !info.isFile() ||
       text_assets::formatFor(info.fileName())==text_assets::Format::Unsupported)
        return false;
    QString const absolute=info.canonicalFilePath();
    if(absolute.isEmpty())return false;
    for(int i=0;i<tabs_->count();++i) {
        if(text_assets::filePath(tabs_->widget(i))==absolute) {
            tabs_->setCurrentIndex(i);
            show();raise();
            return true;
        }
    }
    auto* pane=text_assets::createEmbedded(tabs_,absolute,sharedAsset,
        [this](QString const& saved,bool validatedShader) {
        emit assetSaved(saved,validatedShader);
        },[this](QString const& shader,QByteArray const& spirv) {
        emit shaderCompiled(shader,spirv);
        });
    if(!pane)return false;
    pane->setObjectName("textDocumentPane");
    int const index=tabs_->addTab(pane,info.fileName());
    tabs_->setTabToolTip(index,absolute);
    connect(pane,&QObject::destroyed,this,[this] {
        // Pane lifecycle is owned by QTabWidget.
    });
    // A text document dirty indicator is distinct from the Scene dirty state.
    if(auto* source=pane->findChild<QPlainTextEdit*>("northstarTextAssetSource")) {
        connect(source->document(),&QTextDocument::modificationChanged,this,
                [this,pane,info](bool dirty) {
            int const tab=tabs_->indexOf(pane);
            if(tab>=0)tabs_->setTabText(tab,
                (dirty?"* ":"")+info.fileName());
        });
    }
    tabs_->setCurrentIndex(index);
    show();raise();
    return true;
}
bool QtTextAssetWorkspace::closeDocument(int index) {
    if(index<0 || index>=tabs_->count())return false;
    auto* pane=tabs_->widget(index);
    if(!text_assets::confirmClose(pane))return false;
    tabs_->removeTab(index);
    pane->deleteLater();
    return true;
}
bool QtTextAssetWorkspace::confirmCloseAll() {
    // Check all documents before removing any, to avoid partially closing
    // the workspace when the user presses Cancel on one of the prompts.
    for(int i=0;i<tabs_->count();++i)
        if(!text_assets::confirmClose(tabs_->widget(i)))return false;
    return true;
}
} // namespace northstarcpp::editor
