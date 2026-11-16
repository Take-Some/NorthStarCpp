// Native Qt multi-track cutscene sequencer: UI/atomic save acceptance.
#include "qt_cutscene_timeline.hpp"
#include <northstarcpp/camera_cutscene.hpp>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QFile>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QWidget>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace northstarcpp;
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        QTemporaryDir project;
        if(!project.isValid())return 2;
        QWidget host;host.show();
        int previews=0;
        northstarcpp::editor::openCutsceneTimeline(&host,
            std::filesystem::path(project.path().toStdWString()),
            [&](CameraPose const& camera){
                if(std::isfinite(camera.position.x))++previews;
            });
        QCoreApplication::processEvents();
        auto* dialog=host.findChild<QDialog*>("northstarCutsceneTimeline");
        if(!dialog || !dialog->isVisible())return 3;
        auto* tabs=dialog->findChild<QTabWidget*>("cutsceneTracks");
        auto* shots=dialog->findChild<QTableWidget*>("cutsceneShots");
        auto* keys=dialog->findChild<QTableWidget*>("cutsceneCameraKeys");
        auto* cues=dialog->findChild<QTableWidget*>("cutsceneCues");
        auto* lens=dialog->findChild<QTableWidget*>("cutsceneLens");
        auto* scrub=dialog->findChild<QSlider*>("cutsceneScrub");
        auto* preview=dialog->findChild<QPushButton*>("cutscenePreview");
        auto* save=dialog->findChild<QPushButton*>("cutsceneSave");
        auto* add=dialog->findChild<QPushButton*>("cutsceneAddKey");
        auto* canvas=dialog->findChild<QWidget*>("northstarCutsceneLaneCanvas");
        if(!tabs || tabs->count()!=4 || !shots || shots->rowCount()!=2 ||
           !keys || keys->rowCount()!=4 || !cues || !lens ||
           !scrub || scrub->maximum()<4000 || !preview || !save ||
           !add || !canvas || canvas->grab().isNull())return 4;
        // Native QWidget screenshot and pixel-level visual acceptance:
        // assert that the dark timeline is rendered in the theme color
        // and not a default white unstyled QTableWidget.
        QCoreApplication::processEvents();
        auto const rendered=canvas->grab().toImage();
        if(rendered.isNull() || rendered.width()<250 || rendered.height()<130)
            return 10;
        bool dark=false,playhead=false;
        for(int y=0;y<rendered.height();++y) {
            for(int x=0;x<rendered.width();++x) {
                auto color=rendered.pixelColor(x,y);
                if(color==QColor("#101822"))dark=true;
                if(color==QColor("#ffe174"))playhead=true;
            }
        }
        if(!dark || !playhead)return 11;
        if(argc>1) {
            auto folder=QString::fromLocal8Bit(argv[1]);
            if(!QDir().mkpath(folder) ||
               !dialog->grab().save(QDir(folder).filePath(
                    "northstar_cutscene_sequencer.png")))
                return 12;
        }
        scrub->setValue(2000);
        preview->click();
        if(previews<1)return 5;
        tabs->setCurrentIndex(2);
        int count=cues->rowCount();
        add->click();
        if(cues->rowCount()!=count+1)return 6;
        // New event defaults are a valid track; Save must round-trip through
        // the SAME native JSON decoder used by game-side AssetStore.
        save->click();
        auto filename=project.path()+"/cinematics/intro.camera-cutscene.json";
        QFile file(filename);
        if(!file.open(QIODevice::ReadOnly))return 7;
        auto encoded=file.readAll();
        auto parsed=parse_camera_cutscene(
            std::string(encoded.constData(),std::size_t(encoded.size())));
        if(parsed.size()!=2 || parsed.cues().size()!=1 ||
           parsed.lens_keys().size()!=2 ||
           parsed.cues().front().type!="event")return 8;
        // Opening a second instance of the editor must focus the first one.
        northstarcpp::editor::openCutsceneTimeline(&host,
            std::filesystem::path(project.path().toStdWString()));
        QCoreApplication::processEvents();
        if(host.findChildren<QDialog*>("northstarCutsceneTimeline").size()!=1)
            return 9;
        std::cout<<"NorthStar Qt cinematic sequencer timeline, editor inputs, "
                    "preview, atomic save, VFS JSON roundtrip: PASS\n";
        dialog->close();
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Qt cutscene sequencer QA: "<<e.what()<<'\n';
        return 1;
    }
}
