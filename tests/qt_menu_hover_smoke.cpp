// NorthStarCpp native Qt menu hover acceptance. Verifies actual painted
// highlights in popup QMenu rows and QMenuBar items for both UI themes.
#include "qt_menu_theme.hpp"
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QMainWindow>
#include <QMouseEvent>
#include <QWidget>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <string>
using namespace northstarcpp::editor;

static void require(bool condition,char const* message) {
    if(!condition)throw std::runtime_error(message);
}
static int colorCount(QImage const& image,QRect const& area,QColor desired) {
    int found=0;
    auto clipped=area.intersected(image.rect()).adjusted(2,2,-2,-2);
    for(int y=clipped.top();y<=clipped.bottom();++y)
        for(int x=clipped.left();x<=clipped.right();++x) {
            auto const color=image.pixelColor(x,y);
            if(std::abs(color.red()-desired.red())<=2 &&
               std::abs(color.green()-desired.green())<=2 &&
               std::abs(color.blue()-desired.blue())<=2)++found;
        }
    return found;
}
static void hover(QWidget* widget,QPoint local) {
    auto const pos=QPointF(local);
    auto const global=QPointF(widget->mapToGlobal(local));
    QMouseEvent event(QEvent::MouseMove,pos,global,Qt::NoButton,
                      Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(widget,&event);
    QCoreApplication::processEvents();
    widget->repaint();
    QCoreApplication::processEvents();
}
static void checkTheme(QApplication& app,MenuTheme theme) {
    // Project Browser installs a different light QWidget palette first.
    // The common menu theme must remain effective after that stylesheet.
    app.setStyleSheet(theme==MenuTheme::Light?
        QStringLiteral("QMainWindow,QWidget#root {background:#f6f8fb;"
                       "color:#202b39;} QPushButton:hover {background:#e8f3fc;}"):
        QString{});
    installMenuTheme(app,theme);
    auto const highlight=theme==MenuTheme::Dark?
        QColor("#386e98"):QColor("#cce7f9");
    auto const topHighlight=theme==MenuTheme::Dark?
        QColor("#36668a"):QColor("#d3eafa");
    QMenu menu;
    menu.setTitle("Actions");
    auto* first=menu.addAction("New Scene");
    auto* second=menu.addAction("Open Scene");
    menu.addSeparator();
    auto* disabled=menu.addAction("Unavailable");
    disabled->setEnabled(false);
    auto* submenu=menu.addMenu("Import Assets");
    submenu->addAction("From disk");
    menu.popup({70,70});
    QCoreApplication::processEvents();
    require(menu.isVisible(),"Context menu was not opened");
    auto const area=menu.actionGeometry(second);
    require(!area.isEmpty(),"Context menu action geometry missing");
    menu.setActiveAction(nullptr);
    QCoreApplication::processEvents();
    auto const before=menu.grab().toImage();
    hover(&menu,area.center());
    require(menu.activeAction()==second,"Context menu mouse hover did not activate row");
    auto const after=menu.grab().toImage();
    auto const beforeColored=colorCount(before,area,highlight);
    auto const afterColored=colorCount(after,area,highlight);
    require(afterColored>80 && afterColored>beforeColored+70,
            "Context menu hovered row has no visible background highlight");
    require(menu.actionGeometry(first).isValid(),"First context action lost");
    auto const submenuRect=menu.actionGeometry(submenu->menuAction());
    require(submenuRect.isValid(),"Nested popup submenu action lost");
    menu.setActiveAction(nullptr);
    QCoreApplication::processEvents();
    auto const submenuBefore=menu.grab().toImage();
    hover(&menu,submenuRect.center());
    require(menu.activeAction()==submenu->menuAction(),
            "Parent submenu row did not respond to mouse hover");
    auto const submenuAfter=menu.grab().toImage();
    require(colorCount(submenuAfter,submenuRect,highlight)>
            colorCount(submenuBefore,submenuRect,highlight)+60,
            "Nested submenu trigger does not visibly highlight on hover");
    submenu->popup({260,70});
    QCoreApplication::processEvents();
    auto const nestedRect=submenu->actionGeometry(submenu->actions().front());
    submenu->setActiveAction(nullptr);
    QCoreApplication::processEvents();
    auto const nestedBefore=submenu->grab().toImage();
    hover(submenu,nestedRect.center());
    require(submenu->activeAction()==submenu->actions().front(),
            "Nested menu action did not activate on hover");
    require(colorCount(submenu->grab().toImage(),nestedRect,highlight)>
            colorCount(nestedBefore,nestedRect,highlight)+45,
            "Nested submenu item highlight was not painted");
    submenu->hide();
    menu.hide();

    // QMenuBar::item:selected must also work, independent of QMenu popups.
    QMainWindow window;
    window.resize(430,160);
    // Editor applies an additional window-level stylesheet. It must not
    // override the application menu active-item styling.
    window.setStyleSheet(
        "QMainWindow,QDockWidget{background:#151c26;color:#d6e4f2;}"
        "QStatusBar{background:#1d2a3a;color:#e1ebf4;}"
        "QLabel{color:#acbed1;}");
    auto* top=window.menuBar();
    auto* file=top->addMenu("&File");
    top->addMenu("&Edit");
    window.show();
    QCoreApplication::processEvents();
    auto const fileArea=top->actionGeometry(file->menuAction());
    require(!fileArea.isEmpty(),"Menu bar File geometry missing");
    auto const topBefore=top->grab().toImage();
    hover(top,fileArea.center());
    require(top->activeAction()==file->menuAction(),
            "Menu bar hover did not select File menu");
    auto const topAfter=top->grab().toImage();
    auto const beforeTop=colorCount(topBefore,fileArea,topHighlight);
    auto const afterTop=colorCount(topAfter,fileArea,topHighlight);
    require(afterTop>10 && afterTop>beforeTop+8,
            "Menu bar hovered item not visibly highlighted");
    window.close();
    std::printf("NorthStar menu hover %s: context + menu bar PASS\n",
                theme==MenuTheme::Dark?"dark":"light");
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        checkTheme(app,MenuTheme::Dark);
        checkTheme(app,MenuTheme::Light);
        return 0;
    }catch(std::exception const& error) {
        std::fprintf(stderr,"Menu hover QA failure: %s\n",error.what());
        return 1;
    }
}
