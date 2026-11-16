// NorthStar Qt Inspector layout acceptance: real shared editor scroll helper.
#include "qt_inspector_scroll.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QColor>
#include <QPalette>
#include <QDockWidget>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <iostream>
#include <stdexcept>

using namespace northstarcpp::editor;
namespace {
void check(bool condition,char const* error) {
    if(!condition)throw std::runtime_error(error);
}
QRect globalRect(QWidget const* widget) {
    return QRect(widget->mapToGlobal(QPoint(0,0)),widget->size());
}
}

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        QMainWindow editor;
        editor.setObjectName("editor");
        editor.setCentralWidget(new QWidget(&editor));
        auto* inspector=new QDockWidget("Details | Properties",&editor);
        inspector->setObjectName("inspectorDock");
        inspector->setFeatures(QDockWidget::DockWidgetClosable|
                               QDockWidget::DockWidgetMovable);
        inspector->setAllowedAreas(Qt::LeftDockWidgetArea|
                                   Qt::RightDockWidgetArea);
        auto const elements=createInspectorScroll(inspector);
        // The native platform defaults to a light QScrollArea viewport.
        // Both the viewport and form must explicitly carry the editor's
        // dark palette, independent of QMainWindow/QDockWidget QSS.
        QColor const inspectorDark("#151c26");
        check(elements.panel->objectName()=="inspectorPanel" &&
              elements.search->objectName()=="detailsPropertySearch" &&
              elements.search->isClearButtonEnabled() &&
              elements.scroll->objectName()=="inspectorScrollArea" &&
              elements.scroll->viewport()->objectName()=="inspectorViewport" &&
              elements.content->objectName()=="inspectorFormContent",
              "Inspector dark-theme style selectors changed");
        check(elements.scroll->autoFillBackground() &&
              elements.scroll->viewport()->autoFillBackground() &&
              elements.content->autoFillBackground(),
              "Inspector scroll/viewport/form do not paint their own dark background");
        check(elements.scroll->palette().color(QPalette::Base)==inspectorDark &&
              elements.scroll->viewport()->palette().color(QPalette::Window)==inspectorDark &&
              elements.content->palette().color(QPalette::Window)==inspectorDark,
              "Inspector inherits a light background from the platform theme");
        for(int index=0;index<15;++index) {
            auto* field=new QLineEdit(elements.content);
            field->setObjectName(QString("actorField%1").arg(index));
            elements.form->addRow(
                QString("Actor Property %1").arg(index),field);
        }
        auto* ecs=new QGroupBox("ECS Components",elements.content);
        auto* ecsLayout=new QVBoxLayout(ecs);
        auto* records=new QTreeWidget(ecs);
        records->setHeaderLabels(
            {"Component Type","Version","Bytes","Provider"});
        records->setFixedHeight(144);
        ecsLayout->addWidget(records);
        ecsLayout->addWidget(new QPushButton("Add",ecs));
        elements.form->addRow(ecs);
        auto* apply=new QPushButton("Apply Object Properties",elements.content);
        elements.form->addRow(apply);
        auto* help=new QLabel(
            "Scroll to this help text, even if the Qt Editor is only 460 px tall",
            elements.content);
        help->setWordWrap(true);
        elements.form->addRow(help);
        elements.scroll->setWidget(elements.content);
        inspector->setWidget(elements.panel);
        editor.addDockWidget(Qt::RightDockWidgetArea,inspector);

        auto* browser=new QDockWidget("Content Browser",&editor);
        browser->setObjectName("contentDock");
        browser->setWidget(new QTreeWidget(browser));
        editor.addDockWidget(Qt::BottomDockWidgetArea,browser);
        editor.show();
        // Search is pinned above the scroll viewport and filters by labels.
        elements.search->setText("ECS");
#if QT_VERSION >= QT_VERSION_CHECK(6,4,0)
        check(elements.form->isRowVisible(ecs) &&
              !elements.form->isRowVisible(apply),
              "Details property search did not filter the form");
#endif
        elements.search->clear();
#if QT_VERSION >= QT_VERSION_CHECK(6,4,0)
        check(elements.form->isRowVisible(apply),
              "Clearing property search did not restore hidden rows");
#endif
        QCoreApplication::processEvents(QEventLoop::AllEvents,50);
        auto contentPixels=elements.content->grab(
            QRect(1,1,5,5)).toImage();
        check(!contentPixels.isNull() &&
              contentPixels.pixelColor(2,2)==QColor("#151c26"),
              "Inspector actually renders a light pixel background");
        for(auto size:{QSize(1150,720),QSize(900,460),QSize(780,390)}) {
            editor.resize(size);
            inspector->show();
            browser->show();
            QCoreApplication::processEvents(QEventLoop::AllEvents,50);
            check(inspector->isVisible() && browser->isVisible(),
                  "Inspector or ContentBrowser disappeared");
            check(!globalRect(inspector).intersects(globalRect(browser)),
                  "Inspector overlaps ContentBrowser");
            check(elements.scroll->verticalScrollBar()->maximum()>0,
                  "Inspector does not scroll at a constrained window size");
            elements.scroll->ensureWidgetVisible(help,0,10);
            QCoreApplication::processEvents(QEventLoop::AllEvents,50);
            auto const pos=help->mapTo(elements.scroll->viewport(),
                                        QPoint(0,0));
            check(pos.y()<elements.scroll->viewport()->height() &&
                  pos.y()+help->height()>0,
                  "Inspector help text is unreachable by scrolling");
            elements.scroll->ensureWidgetVisible(apply,0,10);
            QCoreApplication::processEvents(QEventLoop::AllEvents,50);
            auto const action=apply->mapTo(elements.scroll->viewport(),
                                            QPoint(0,0));
            check(action.y()<elements.scroll->viewport()->height() &&
                  action.y()+apply->height()>0,
                  "Apply Object Properties is unreachable by scrolling");
        }
        check(inspector->minimumHeight()<=80,
              "Inspector enforces full form height on dock splitter");
        std::cout<<"PASS: Qt Inspector dark palette, scroll viewport and form, "
                    "ECS and action reachability, three viewport sizes, "
                    "no ContentBrowser overlap\n";
        return 0;
    }catch(std::exception const& error) {
        std::cerr<<"Inspector layout QA: "<<error.what()<<'\n';
        return 1;
    }
}
