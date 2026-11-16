#pragma once
#include "qt_content_browser.hpp"
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
namespace northstarcpp::editor {
class QtContentDrawer final : public QFrame {
public:
    explicit QtContentDrawer(QWidget* editor):QFrame(editor) {
        setObjectName("contentDrawerOverlay");
        setFrameShape(QFrame::StyledPanel);
        setAttribute(Qt::WA_StyledBackground,true);
        setStyleSheet("QFrame#contentDrawerOverlay {background:#151a22;"
                      "border:1px solid #5c7490;border-radius:5px;}");
        auto* root=new QVBoxLayout(this);
        root->setContentsMargins(5,5,5,5);
        auto* bar=new QHBoxLayout;
        auto* title=new QLabel("CONTENT DRAWER",this);
        bar->addWidget(title);bar->addStretch();
        auto* close=new QPushButton("Close [Esc]",this);
        close->setObjectName("contentDrawerClose");
        connect(close,&QPushButton::clicked,this,&QtContentDrawer::hide);
        bar->addWidget(close);
        root->addLayout(bar);
        browser_=new QtContentBrowser(this);
        browser_->setObjectName("contentDrawerBrowser");
        browser_->setEditorDockMode(true);
        root->addWidget(browser_,1);
        hide();
        if(editor)editor->installEventFilter(this);
        if(qApp)qApp->installEventFilter(this);
    }
    ~QtContentDrawer() override {
        if(qApp)qApp->removeEventFilter(this);
        if(parentWidget())parentWidget()->removeEventFilter(this);
    }
    QtContentBrowser* contentBrowser() const {return browser_;}
    void setRoots(QString const& project,QString const& shared) {
        if(browser_->sharedContentRoot()!=shared)
            browser_->setSharedContentRoot(shared);
        if(browser_->contentRoot()!=project)
            browser_->setContentRoot(project);
    }
    void showDrawer() {
        fitToEditor();show();raise();
        QTimer::singleShot(0,this,[this] {
            if(isVisible())browser_->focusSearch();
        });
    }
    void toggleDrawer() {
        if(isVisible())hide();else showDrawer();
    }
protected:
    bool eventFilter(QObject* source,QEvent* event) override {
        if(event->type()==QEvent::Resize && source==parentWidget() &&
           isVisible())fitToEditor();
        if(!isVisible())return QFrame::eventFilter(source,event);
        if(event->type()==QEvent::KeyPress &&
           static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) {
            hide();return true;
        }
        if(event->type()==QEvent::MouseButtonPress) {
            auto* target=qobject_cast<QWidget*>(source);
            if(target && target->window()==window() &&
               target!=this && !isAncestorOf(target))
                hide();
        }
        return QFrame::eventFilter(source,event);
    }
private:
    void fitToEditor() {
        auto* host=parentWidget();
        if(!host)return;
        int h=std::max(255,std::min(530,host->height()*52/100));
        int w=std::max(650,host->width()-24);
        setGeometry(12,std::max(38,host->height()-h-32),w,h);
    }
    QtContentBrowser* browser_{};
};
}
