// Headless UI acceptance for editor missing-resource warning cards.
#include "qt_asset_warnings.hpp"
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <stdexcept>
#include <cstdio>
using northstarcpp::editor::AssetWarning;
using northstarcpp::editor::AssetWarningKind;
using northstarcpp::editor::AssetWarningPanel;
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        QWidget dialog;
        AssetWarningPanel warnings(&dialog);
        warnings.setWarnings({
            {AssetWarningKind::Texture,"textures/skydome.ytd@moon_new","Texture entry not found"},
            {AssetWarningKind::Material,"materials/character.ymat","Material binding unresolved"},
            {AssetWarningKind::Model,"models/character.ydd","Model file not found"}
        });
        if(warnings.warningCount()!=3)throw std::runtime_error("Warning count");
        auto cards=warnings.findChildren<QFrame*>("assetWarningCard");
        if(cards.size()!=3)throw std::runtime_error("Warning cards were not rendered");
        auto* title=cards[0]->findChild<QLabel*>("assetWarningTitle");
        if(!title || title->text()!="Missing texture")
            throw std::runtime_error("Warning title not rendered");
        auto* icon=cards[0]->findChild<QLabel*>("assetWarningIcon");
        if(!icon || icon->pixmap().isNull())
            throw std::runtime_error("Warning icon was not rendered");
        auto* reference=cards[0]->findChild<QLabel*>("assetWarningReference");
        if(!reference || reference->text()!="textures/skydome.ytd@moon_new" ||
           !(reference->textInteractionFlags()&Qt::TextSelectableByMouse))
            throw std::runtime_error("Full selectable texture reference missing");
        auto* copy=cards[0]->findChild<QPushButton*>("assetWarningCopyReference");
        if(!copy)throw std::runtime_error("Warning copy button missing");
        copy->click();
        if(QApplication::clipboard()->text()!=reference->text())
            throw std::runtime_error("Resource copy failed");
        if(cards[1]->findChild<QLabel*>("assetWarningTitle")->text()!="Missing material" ||
           cards[2]->findChild<QLabel*>("assetWarningTitle")->text()!="Missing model")
            throw std::runtime_error("Material/model warnings not typed");
        warnings.setWarnings({});
        if(warnings.warningCount()!=0 ||
           !warnings.findChildren<QFrame*>("assetWarningCard").empty())
            throw std::runtime_error("Stale warnings after successful asset reload");
        std::puts("NorthStar Qt Asset Warning Cards: PASS");
        return 0;
    }catch(std::exception const& e) {
        std::fprintf(stderr,"Asset Warning Cards failure: %s\n",e.what());
        return 1;
    }
}
