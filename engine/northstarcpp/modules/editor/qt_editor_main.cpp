#include "qt_editor_window.hpp"
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QTemporaryDir>
#include <QStatusBar>
#include <QFileInfo>
#include <QTimer>
#include <iostream>
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setApplicationName("NorthStar Qt Editor");
    app.setOrganizationName("Kayla's Systems");
    QCommandLineParser parser;
    parser.setApplicationDescription("Cross-platform Qt 6 NorthStar editor shell");
    parser.addHelpOption();
    QCommandLineOption scene({"s","scene"},"Scene file to open (.nscn/.nscene).","file");
    QCommandLineOption smoke("smoke","Run dock/scene/binary/asset-drop smoke.");
    QCommandLineOption gpuSmoke("gpu-smoke",
        "Render one real engine.render GPU frame inside Qt native viewport.");
    QCommandLineOption smokeOutput("smoke-output",
        "Save a valid binary scene at FILE for packaged-editor smoke.", "file");
    QCommandLineOption playerCheck("player-check",
        "Verify installed Player can load a scene authored by this editor.");
    parser.addOption(scene);
    parser.addOption(smoke);
    parser.addOption(gpuSmoke);
    parser.addOption(smokeOutput);
    parser.addOption(playerCheck);
    parser.process(app);
    northstarcpp::editor::QtEditorWindow window;
    if(parser.isSet(scene) && !window.openScene(parser.value(scene)))
        return 2;
    window.show();
    if(parser.isSet(smokeOutput)) {
        if(!parser.isSet(smoke)) {
            std::cerr<<"--smoke-output requires --smoke"<<std::endl;return 5;
        }
        bool ok=window.saveScene(parser.value(smokeOutput));
        QCoreApplication::processEvents();
        std::cout<<"Qt editor binary scene smoke: "<<(ok?"PASS":"FAIL")
                 <<std::endl;
        return ok?0:6;
    }
    if(parser.isSet(gpuSmoke)) {
        QTemporaryDir temp;
        bool ok=temp.isValid() && window.runGpuSmoke(temp.path());
        QCoreApplication::processEvents();
        std::cout<<"Qt engine.render native GPU viewport: "
                 <<(ok?"PASS":"FAIL")<<std::endl;
        return ok?0:8;
    }
    if(parser.isSet(playerCheck)) {
        QTemporaryDir temp;
        return temp.isValid() && window.runPlayerCheckSmoke(temp.path())?0:7;
    }
    if(parser.isSet(smoke)) {
        QTemporaryDir temporary;
        if(!temporary.isValid())return 3;
        bool ok=window.runSmoke(temporary.path());
        QCoreApplication::processEvents();
        if(!ok)std::cerr<<"Qt editor smoke failed: "<<window.statusBar()->currentMessage().toStdString()<<std::endl;
        std::cout<<"Qt unified editor + scene + asset placement + undo/redo: "
                 <<(ok?"PASS":"FAIL")<<std::endl;
        return ok?0:4;
    }
    return app.exec();
}
