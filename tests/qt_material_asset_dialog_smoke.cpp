// Material asset double-click regression: a native YMAT must open a tabbed
// material editor with Preview + Material Graph, never the old generic preview.
#include "qt_asset_preview_dialog.hpp"
#include "qt_content_browser.hpp"
#include "qt_material_authoring.hpp"
#include "editor_asset_material_source.hpp"
#include <northstarcpp/material_ymat_authoring.hpp>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
using namespace northstarcpp::editor;
static void check(bool yes,std::string const& error) {
    if(!yes)throw std::runtime_error(error);
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    if(argc!=2)return 2;
    try {
        fs::path root=fs::u8path(argv[1]);
        auto reference=QStringLiteral("materials/SoftTintDemo.ymat");
        auto doc=material_authoring::load_descriptor(root,reference);
        check(doc.value("graph").isObject(),
              "Native YMAT authoring descriptor did not decode");
        auto native=northstarcpp::shared_assets::encode_editor_ymat(
            QJsonDocument(doc).toJson(QJsonDocument::Compact).toStdString());
        check(native.size()>=128 &&
              std::string(native.begin(),native.begin()+4)=="NEF8",
              "Material authoring write path did not produce native NEF8");
        auto decoded=northstarcpp::shared_assets::decode_editor_ymat(native);
        check(QJsonDocument::fromJson(QByteArray::fromStdString(decoded)).isObject(),
              "Native YMAT editor roundtrip lost descriptor");

        // Multi-entry NEF8 must remain a browsable library. Both names
        // and the exact selected material (not entry zero) are validated.
        fs::path libraryRoot=root.parent_path().parent_path().parent_path()/
                             "tests"/"fixtures";
        auto const library=QStringLiteral("materials/MultiMaterialDemo.ymat");
        auto const selected=library+QStringLiteral("@Blue Ceramic");
        QtContentBrowser browser;
        browser.setContentRoot(QString::fromStdWString(libraryRoot.wstring()));
        // Indexing is QtConcurrent-backed; a real user cannot double-click
        // until the matching card has appeared.
        QElapsedTimer indexing;
        indexing.start();
        while(browser.recordCount()==0 && indexing.elapsed()<8000) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        check(browser.recordCount()>0,"Content Browser catalog did not finish indexing");
        int requests=0;
        QObject::connect(&browser,&QtContentBrowser::materialEditorRequested,
                         &browser,[&](QString value) {
            check(value==selected,"Double-click selected wrong material");
            ++requests;
        });
        // Opening a YMAT library must display all named NEF8 entries, not
        // dispatch a first-material editor by default.
        browser.openAssetPreview(library);
        check(requests==0,
              "YMAT container incorrectly opened its first material");
        check(browser.visibleCount()==2,
              "NEF8 material library did not show both material entries");
        browser.openAssetPreview(library+QStringLiteral("/Blue Ceramic"));
        check(requests==1,
              "Named material double-click did not dispatch its exact entry");
        auto blue=loadMaterialPreviewSource(libraryRoot,selected.toStdString());
        auto copper=loadMaterialPreviewSource(libraryRoot,
            (library+QStringLiteral("@Copper Panel")).toStdString());
        check(blue.material && copper.material &&
              blue.material->name=="Blue Ceramic" &&
              copper.material->name=="Copper Panel",
              "YMAT preview ignored selected material name");
        bool rejectedLibrary=false;
        try {
            (void)loadMaterialPreviewSource(libraryRoot,library.toStdString());
        }catch(std::exception const&) {rejectedLibrary=true;}
        check(rejectedLibrary,
              "Multimaterial YMAT silently resolved the first material");

        bool verified=false;
        QTimer watch;
        watch.setInterval(35);
        QObject::connect(&watch,&QTimer::timeout,&watch,[&] {
            auto* dlg=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dlg)return;
            if(dlg->objectName()!="northstarMaterialAssetEditor") {
                dlg->reject();return;
            }
            auto* tabs=dlg->findChild<QTabWidget*>("materialAssetEditorTabs");
            auto* sphere=dlg->findChild<QLabel*>("materialAssetPreviewSphere");
            auto* graph=dlg->findChild<QWidget*>("materialAssetGraphCanvas");
            auto* save=dlg->findChild<QPushButton*>("materialAssetSaveYmat");
            verified=tabs&&tabs->count()==3&&
                tabs->tabText(0)=="Preview" &&
                tabs->tabText(1)=="Surface" &&
                tabs->tabText(2)=="Material Graph" &&
                graph&&sphere&&save&&save->isEnabled()&&
                dlg->findChild<QWidget*>("materialParameterTable")&&
                dlg->findChild<QWidget*>("materialTextureTable");
            dlg->accept();
        });
        QTimer::singleShot(9000,&app,[&] {
            auto* dlg=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(dlg)dlg->reject();
        });
        watch.start();
        auto result=openAssetPreviewModal(nullptr,root,
            "materials/SoftTintDemo.ymat",AssetKind::material);
        watch.stop();
        check(result==QDialog::Accepted&&verified,
              "YMAT double-click did not open editable Preview/Surface/Graph Editor");
        std::cout<<"Native YMAT library browsing, selected entry and editable Graph GUI: PASS\n";
        return 0;
    }catch(std::exception const& error) {
        std::cerr<<"Native YMAT dialog QA failed: "<<error.what()<<"\n";
        return 1;
    }
}
