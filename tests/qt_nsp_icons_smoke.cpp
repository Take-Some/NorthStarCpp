#include "qt_nsp_icons.hpp"
#include <northstarcpp/assets.hpp>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QResource>
#include <QImage>
#include <QString>
#include <iostream>
#include <array>

int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    if(!northstarcpp::editor::ensure_northstar_qt_icons()) {
        std::cerr<<"NSP Qt resource registration failed\n";
        return 1;
    }
    static constexpr std::array<const char*,22> browser{{
        "animFile.png","animFolder.png","collisionDir.png",
        "collisionFile.png","fileJs.png","fileMd.png","fileNui.png","folder.png",
        "fragShader.png","materialFunc.png","materialsFolder.png",
        "modelsFolder.png","otherFile.png","skeletalFolder.png",
        "skelFile.png","soundFile.png","soundsFolder.png",
        "spvShader.png","texturesFolder.png","vertShader.png",
         "fileYpt.png","yptFolder.png"}};
    static constexpr std::array<const char*,9> playback{{
        "eject-active.png","eject-disabled.png","pause-active.png",
        "play.png","play_disabled_64.png","resume-active.png",
        "resume-disabled.png","stop-active.png","stop-disabled.png"}};
    auto check=[](QString const& name)->bool {
        QResource const entry(name);
        QImage const pixels(name);
        if(!entry.isValid() || pixels.isNull() ||
           pixels.width()<1 || pixels.height()<1) {
            std::cerr<<"NSP UI icon unavailable: "<<name.toStdString()<<"\n";
            return false;
        }
        return true;
    };
    for(auto const* name:browser) {
        auto const path=QStringLiteral(":/northstar/icons/assetBrowser/") +
                        QString::fromLatin1(name);
        if(!check(path))return 2;
        QImage const pixels(path);
        // Existing high-resolution YPT artwork is 1254x1254; Qt scales
        // it on demand for cards rather than storing an extra resized copy.
        const bool particle=(QString::fromLatin1(name)=="fileYpt.png" ||
                             QString::fromLatin1(name)=="yptFolder.png");
        if(particle && (pixels.width()<256 || pixels.width()!=pixels.height())) {
            std::cerr<<"YPT icon is not a square high-resolution image: "<<name<<"\n";
            return 5;
        }
        if(particle)continue;
        if(pixels.size()!=QSize(256,256)) {
            std::cerr<<"Incorrect asset icon canvas: "<<name<<"\n";
            return 5;
        }
        // Ensure new artwork preserves the apparent scale of the family.
        // Ignore soft outer glow (alpha <= 15%) when measuring bounds.
        int left=256,top=256,right=-1,bottom=-1;
        for(int y=0;y<pixels.height();++y)
            for(int x=0;x<pixels.width();++x)
                if(qAlpha(pixels.pixel(x,y))>38) {
                    left=qMin(left,x);top=qMin(top,y);
                    right=qMax(right,x);bottom=qMax(bottom,y);
                }
        auto const extent=qMax(right-left+1,bottom-top+1);
        if(right<0 || extent<222 || extent>227) {
            std::cerr<<"Asset icon visual scale mismatch: "<<name
                     <<" (extent "<<extent<<" px)\n";
            return 6;
        }
    }
    for(auto const* name:playback)
        if(!check(QStringLiteral(":/northstar/play/") +
                  QString::fromLatin1(name)))return 3;
    // The same shared NSP must also contain an actual UI Document asset.
    northstarcpp::AssetStore assets;
    assets.mount_runtime_archives();
    auto const nui=assets.read("ui/default_menu.nui");
    QJsonDocument const document=QJsonDocument::fromJson(
        QByteArray(reinterpret_cast<char const*>(nui.data()),
                   static_cast<qsizetype>(nui.size())));
    if(nui.empty() || !document.isObject() ||
       !document.object().value("schema").toString().startsWith("northstar.ui/")) {
        std::cerr<<"NSP UI Document missing or invalid: ui/default_menu.nui\n";
        return 4;
    }
    std::cout<<"NSP Qt UI: 22 Asset Browser + 9 Play icons and .nui asset validated\n";
    return 0;
}
