#include "qt_particle_simulation_dialog.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QRegularExpression>
#include <QWidget>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    if(argc!=2)return 2;
    using northstarcpp::editor::openParticleSimulationPreview;
    int successful=0;
    QImage before;
    QTimer::singleShot(220,[&] {
        for(auto* dialog:QApplication::topLevelWidgets())
            if(dialog->objectName()=="nativeParticleSimulationPreviewDialog")
                if(auto* canvas=dialog->findChild<QWidget*>("particleSimulationCanvas"))
                    before=canvas->grab().toImage();
    });
    QTimer::singleShot(520,[&] {
        for(auto* widget:QApplication::topLevelWidgets()) {
            if(widget->objectName()!="nativeParticleSimulationPreviewDialog")
                continue;
            auto* canvas=widget->findChild<QWidget*>("particleSimulationCanvas");
            auto* selector=widget->findChild<QComboBox*>("particleEffectSelector");
            auto* status=widget->findChild<QLabel*>("particleSimulationStatus");
            auto* play=widget->findChild<QPushButton*>("particlePauseButton");
            auto const match=QRegularExpression(
                "Active particles: ([0-9]+)").match(
                    status?status->text():QString{});
            std::cerr<<"FXDEBUG status="
                <<(status?status->text().toStdString():"null")
                <<" selection="
                <<(selector?selector->currentData().toString().toStdString():"null")
                <<" before="<<before.width()<<"x"<<before.height()<<"\n";
            if(canvas && selector && status && play &&
               selector->count()>=48 &&
               selector->currentData().toString().startsWith("effect.") &&
               match.hasMatch() && match.captured(1).toInt()>0) {
                auto const after=canvas->grab().toImage();
                int changed=0;
                if(!before.isNull() && before.size()==after.size())
                    for(int y=90;y<after.height()-90;y+=4)
                        for(int x=50;x<after.width()-50;x+=4)
                            if(before.pixel(x,y)!=after.pixel(x,y))++changed;
                std::cerr<<"FXDEBUG changed="<<changed<<"\n";
                if(changed>8 && after.width()>100 && after.height()>100) {
                    successful=1;
                    selector->setCurrentIndex(1);
                    play->click();
                    if(play->text()=="Play")++successful;
                    auto* restart=widget->findChild<QPushButton*>(
                        "particleRestartButton");
                    if(restart){restart->click();++successful;}
                }
            }
            widget->close();
        }
    });
    QTimer::singleShot(1800,[&] {
        for(auto* widget:QApplication::topLevelWidgets())
            if(widget->objectName()=="nativeParticleSimulationPreviewDialog")
                widget->close();
    });
    auto const result=openParticleSimulationPreview(nullptr,argv[1],
        "particles/core.ypt@effect.ent_amb_drain_splash");
    if(successful!=3) {
        std::cerr<<"Qt YPT simulation animated-pixel test failed: "<<successful
                 <<" result="<<result<<"\n";
        return 1;
    }
    std::cout<<"Qt YPT live preview: effect selector, 3D canvas, "
                "timer, pause and restart PASS\n";
    return 0;
}
