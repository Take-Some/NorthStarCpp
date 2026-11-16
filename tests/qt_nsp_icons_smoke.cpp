#include "qt_nsp_icons.hpp"
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
    static constexpr std::array<const char*,19> browser{{
        "animFile.png","animFolder.png","collisionDir.png",
        "collisionFile.png","fileJs.png","fileMd.png","folder.png",
        "fragShader.png","materialFunc.png","materialsFolder.png",
        "modelsFolder.png","otherFile.png","skeletalFolder.png",
        "skelFile.png","soundFile.png","soundsFolder.png",
        "spvShader.png","texturesFolder.png","vertShader.png"}};
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
    for(auto const* name:browser)
        if(!check(QStringLiteral(":/northstar/icons/assetBrowser/") +
                  QString::fromLatin1(name)))return 2;
    for(auto const* name:playback)
        if(!check(QStringLiteral(":/northstar/play/") +
                  QString::fromLatin1(name)))return 3;
    std::cout<<"NSP Qt UI: 19 Asset Browser + 9 Play icons loaded, decoded and validated\n";
    return 0;
}
