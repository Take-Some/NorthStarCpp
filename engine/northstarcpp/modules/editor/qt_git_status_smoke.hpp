#pragma once
#include "qt_content_browser.hpp"
#include "qt_asset_card_delegate.hpp"
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QListView>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
namespace northstarcpp::editor::git_browser_qa {
inline bool prepare(QTemporaryDir& temp,QtContentBrowser& browser) {
    if(!temp.isValid() || !QDir().mkpath(temp.path()+"/assets"))
        return false;
    QProcess git;
    git.setWorkingDirectory(temp.path());
    git.start("git",{"init","-q"});
    if(!git.waitForFinished(5000) || git.exitCode()!=0)return false;
    QFile file(temp.path()+"/assets/visible.ydr");
    if(!file.open(QIODevice::WriteOnly) || file.write("asset")==-1)
        return false;
    file.close();
    browser.setContentRoot(temp.path()+"/assets");
    return true;
}
inline bool verify(QtContentBrowser& browser) {
    auto* view=browser.findChild<QListView*>("assetGrid");
    if(!view || !view->model())return false;
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<9000) {
        QCoreApplication::processEvents();
        if(view->model()->rowCount()==1) {
            auto index=view->model()->index(0,0);
            QString status=index.data(asset_card_roles::git_status).toString();
            if(status=="??") {
                std::cout<<"PASS Git untracked badge cached in tile model\n";
                return true;
            }
        }
        QThread::msleep(15);
    }
    std::cout<<"FAIL Git badge did not populate from project repository\n";
    return false;
}
}
