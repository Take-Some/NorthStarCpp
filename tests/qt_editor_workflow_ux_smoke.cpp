#include "../engine/northstarcpp/modules/editor/qt_editor_workflow_ux.hpp"
#include <QApplication>
#include <QDialog>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QToolBar>
#include <QWidget>
#include <cassert>
#include <iostream>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QMainWindow main;
    main.setObjectName("northstarEditorUxTest");
    auto* viewport=new QWidget(&main);
    main.setCentralWidget(viewport);
    auto* authoring=new QToolBar("Authoring",&main);
    authoring->setObjectName("authoringToolbar");
    main.addToolBar(authoring);
    QDockWidget outliner("Scene Outliner",&main);
    outliner.setObjectName("outlinerDock");
    main.addDockWidget(Qt::LeftDockWidgetArea,&outliner);
    outliner.setWidget(new QLabel("World",&outliner));
    QDockWidget details("Details | Properties",&main);
    details.setObjectName("inspectorDock");
    main.addDockWidget(Qt::RightDockWidgetArea,&details);
    details.setWidget(new QLabel("Properties",&details));
    QDockWidget browser("Content Browser",&main);
    browser.setObjectName("contentDock");
    main.addDockWidget(Qt::BottomDockWidgetArea,&browser);
    browser.setWidget(new QLabel("Assets",&browser));
    int saveCount=0;
    auto* file=main.menuBar()->addMenu("File");
    auto* save=file->addAction("Save Scene");
    save->setObjectName("saveSceneAction");
    QObject::connect(save,&QAction::triggered,&main,
                     [&] {++saveCount;});
    main.resize(900,620);
    main.show();
    app.processEvents();
    northstarcpp::editor::workflow::install(
        &main,&browser,&outliner,&details,viewport);
    app.processEvents();
    auto* workspace=main.findChild<QMenu*>("northstarUxWorkspaceMenu");
    assert(workspace);
    auto* drawer=main.findChild<QAction*>("northstarContentDrawerAction");
    auto* focus=main.findChild<QAction*>("northstarFocusViewportAction");
    auto* palette=main.findChild<QAction*>("northstarCommandPaletteAction");
    assert(drawer && focus && palette);
    assert(browser.isVisible());
    drawer->trigger();
    app.processEvents();
    assert(!browser.isVisible());
    drawer->trigger();
    app.processEvents();
    assert(browser.isVisible());

    assert(outliner.isVisible() && details.isVisible());
    focus->trigger();
    app.processEvents();
    assert(!outliner.isVisible() && !details.isVisible() &&
           !browser.isVisible());
    focus->trigger();
    app.processEvents();
    assert(outliner.isVisible() && details.isVisible() &&
           browser.isVisible());

    palette->trigger();
    app.processEvents();
    auto* dialog=main.findChild<QDialog*>("northstarUxCommandPalette");
    assert(dialog && dialog->isVisible());
    auto* query=dialog->findChild<QLineEdit*>();
    auto* results=dialog->findChild<QListWidget*>();
    assert(query && results);
    query->setText("Save Scene");
    app.processEvents();
    assert(results->count()>0);
    assert(results->currentItem()->text().contains("Save Scene"));
    bool const invoked=QMetaObject::invokeMethod(
        query,"returnPressed",Qt::DirectConnection);
    assert(invoked && saveCount==1);
    assert(!dialog->isVisible());
    assert(main.findChild<QToolBar*>("authoringToolbar")->actions().size()>0);
    std::cout<<"PASS: NorthStar editor toolbar, command palette, real Save QAction, "
                "content drawer, F11 focus and restore\n";
    return 0;
}
