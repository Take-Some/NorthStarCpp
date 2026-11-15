// Full modal acceptance: asset decoding failures must render visible,
// selectable and categorized warning cards, never plain "Missing ..." metadata.
#include "qt_asset_preview_dialog.hpp"
#include "qt_asset_warnings.hpp"
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace northstarcpp::editor;

static void require(bool ok,char const* message) {
    if(!ok)throw std::runtime_error(message);
}
static void expect_missing(std::filesystem::path const& content,
                           std::string const& reference,AssetKind kind,
                           QString const& expected) {
    QElapsedTimer clock;
    clock.start();
    QTimer polling;
    bool found=false;
    polling.setInterval(25);
    QObject::connect(&polling,&QTimer::timeout,&polling,[&] {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog)return;
        auto* panel=dynamic_cast<AssetWarningPanel*>(dialog->findChild<QWidget*>("assetPreviewWarnings"));
        if(panel && panel->warningCount()>=1) {
            auto* title=panel->findChild<QLabel*>("assetWarningTitle");
            auto* resource=panel->findChild<QLabel*>("assetWarningReference");
            auto* icon=panel->findChild<QLabel*>("assetWarningIcon");
            found=title && title->text()==expected &&
                resource && resource->text()==QString::fromStdString(reference) &&
                icon && !icon->pixmap().isNull();
            dialog->accept();
        }else if(clock.elapsed()>6500)dialog->reject();
    });
    polling.start();
    int result=openAssetPreviewModal(nullptr,content,reference,kind);
    polling.stop();
    require(result==QDialog::Accepted && found,
            "Modal Asset Preview did not render a categorized resource warning");
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        // An existing root with non-existing children is sufficient.
        auto const content=std::filesystem::temp_directory_path();
        expect_missing(content,"textures/skydome.ytd@moon_new",
                       AssetKind::texture,"Missing texture");
        expect_missing(content,"models/MissingPlayer.ydd",
                       AssetKind::mesh,"Missing model");
        expect_missing(content,"Materials/Missing.material.json",
                       AssetKind::material,"Missing material");
        std::cout<<"Qt Asset Preview missing-resource warning cards: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Qt Asset Preview warning integration failed: "<<e.what()<<"\n";
        return 1;
    }
}
