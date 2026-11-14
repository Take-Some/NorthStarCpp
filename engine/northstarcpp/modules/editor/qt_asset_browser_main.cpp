// NorthStar native Qt 6 Content Browser host.
// Reusable QtContentBrowser can be docked in a future Qt editor QMainWindow.
#include "qt_content_browser.hpp"
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListView>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QIcon>
#include <iostream>
#include <memory>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setApplicationName("NorthStar Content Browser");
    app.setOrganizationName("Kayla's Systems");
    QCommandLineParser parser;
    parser.setApplicationDescription("Native, cross-platform NorthStar asset browser");
    parser.addHelpOption();
    QCommandLineOption scene({"p","project"},
        "Project scene file whose sibling assets folder will be browsed.","scene");
    QCommandLineOption content({"c","content"},
        "Explicit Content directory to browse.","directory");
    QCommandLineOption smoke("smoke","Run Qt asset-browser integration smoke and exit.");
    QCommandLineOption screenshot("screenshot",
        "Save a screenshot after the asset catalog finishes indexing.","filename");
    parser.addOption(scene);
    parser.addOption(content);
    parser.addOption(smoke);
    parser.addOption(screenshot);
    parser.process(app);
    auto browser=std::make_unique<northstarcpp::editor::QtContentBrowser>();
    std::unique_ptr<QTemporaryDir> sandbox;
    if(parser.isSet(smoke) && !parser.isSet(scene) && !parser.isSet(content)) {
        sandbox=std::make_unique<QTemporaryDir>();
        if(!sandbox->isValid())return 3;
        QDir(sandbox->path()).mkpath("assets/Meshes");
        QDir(sandbox->path()).mkpath("assets/Textures");
        QFile a(sandbox->path()+"/assets/Meshes/npc.ydd");
        QFile b(sandbox->path()+"/assets/Textures/albedo.ytd");
        QFile c(sandbox->path()+"/assets/gameplay.ysc");
        if(!a.open(QIODevice::WriteOnly) || !b.open(QIODevice::WriteOnly) ||
           !c.open(QIODevice::WriteOnly)) return 4;
        a.write("mesh");b.write("texture");c.write("script");
        a.close();b.close();c.close();
        browser->setContentRoot(sandbox->path()+"/assets");
    } else if(parser.isSet(content))
        browser->setContentRoot(parser.value(content));
    else if(parser.isSet(scene))
        browser->setProjectScene(parser.value(scene));

    QMainWindow window;
    window.setObjectName("northstarAssetBrowserWindow");
    window.setWindowTitle("NorthStar Engine — Content Browser");
    window.resize(1460,880);
    window.setMinimumSize(950,550);
    // The native browser is a QWidget; the Qt editor can place it into any
    // QDockWidget or tabbed document area without changing its implementation.
    window.setCentralWidget(browser.get());
    browser.release(); // ownership moves to QMainWindow
    auto* view=static_cast<northstarcpp::editor::QtContentBrowser*>(window.centralWidget());
    QObject::connect(view,&northstarcpp::editor::QtContentBrowser::assetSelected,
        &window,[&window](QString const& reference) {
        window.statusBar()->showMessage(reference.isEmpty()?
            "Content ready":"Selected: Content / "+reference);
    });
    QObject::connect(view,&northstarcpp::editor::QtContentBrowser::assetActivated,
        &window,[&window](QString const& reference) {
        window.statusBar()->showMessage("Asset: Content / "+reference,8000);
    });
    auto* fileMenu=window.menuBar()->addMenu("&Content");
    fileMenu->addAction("&Refresh",QKeySequence(Qt::Key_F5),
        view,&northstarcpp::editor::QtContentBrowser::refresh);
    fileMenu->addSeparator();
    fileMenu->addAction("&Close",QKeySequence::Close,&window,&QMainWindow::close);
    auto* helpMenu=window.menuBar()->addMenu("&Help");
    helpMenu->addAction("About Content Browser",&window,[&window] {
        QMessageBox::about(&window,"NorthStar Content Browser",
            "Native C++/Qt6 asset browser\n"
            "Project-scoped index, editable content, and asynchronous previews.\n"
            "Kayla's Systems — 2026");
    });
    window.statusBar()->showMessage("Native Content Browser  |  F5 Refresh  |  "
        "Double-click to open folders  |  Right-click for asset operations");
    window.show();

    if(parser.isSet(smoke) || parser.isSet(screenshot)) {
        QElapsedTimer elapsed;
        elapsed.start();
        while(elapsed.elapsed()<10000 &&
              view->recordCount()==0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents,50);
            QThread::msleep(10);
        }
        if(parser.isSet(smoke)) {
            bool okay=true;
            if(parser.isSet(content)) {
                auto* grid=view->findChild<QListView*>("assetGrid");
                okay=grid && grid->model();
                view->browseTo("textures/skydome.ytd");
                okay=okay && view->visibleCount()==5;
                if(okay) {
                    for(int i=0;i<grid->model()->rowCount();++i) {
                        auto index=grid->model()->index(i,0);
                        grid->setCurrentIndex(index);
                        auto reference=view->selectedAsset();
                        okay=okay && reference.startsWith("textures/skydome.ytd@");
                    }
                }
                view->browseTo("models/skydome.ydd");
                okay=okay && view->visibleCount()==1;
                if(okay) {
                    grid->setCurrentIndex(grid->model()->index(0,0));
                    okay=view->selectedAsset().startsWith("models/skydome.ydd@");
                }
            } else {
                okay=view->recordCount()==5 && view->visibleCount()==3;
                view->setSearchText("albedo");
                okay=okay&&view->visibleCount()==1;
                view->setSearchText("");
                view->setTypeFilter("Textures");
                okay=okay&&view->visibleCount()==2;
                view->setTypeFilter("All Assets");
            }
            QCoreApplication::processEvents();
            std::cout<<"Qt native content browser smoke: "
                     <<(okay?"PASS":"FAIL")<<"\n";
            return okay?0:5;
        }
        if(parser.isSet(screenshot)) {
            QCoreApplication::processEvents();
            if(!window.grab().save(parser.value(screenshot)))
                return 6;
            return 0;
        }
    }
    return app.exec();
}
