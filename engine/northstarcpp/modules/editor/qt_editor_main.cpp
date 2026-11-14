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
    parser.addOption(scene);parser.addOption(smoke);
    parser.process(app);
    northstarcpp::editor::QtEditorWindow window;
    if(parser.isSet(scene) && !window.openScene(parser.value(scene)))
        return 2;
    window.show();
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
