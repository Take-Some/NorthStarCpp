#pragma once
#include "qt_content_browser.hpp"
#include <northstarcpp/nac.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QIcon>
#include <QListView>
#include <QLabel>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <iostream>
namespace northstarcpp::editor::nac_browser_qa {
inline bool prepare(QTemporaryDir& temp,QtContentBrowser& browser) {
    if(!temp.isValid())return false;
    nac::Track a; a.id=42; a.name="Engine"; a.channels=1;
    a.samples.assign(512,100);
    nac::Track b; b.id=91; b.name="Horn"; b.channels=2;
    b.samples.assign(1024,500);
    std::vector<std::uint8_t> data;
    std::string error;
    if(!nac::encode({a,b},data,error))return false;
    QFile file(temp.path()+"/vehicle.nac");
    if(!file.open(QIODevice::WriteOnly) ||
       file.write(reinterpret_cast<char const*>(data.data()),data.size()) !=
       static_cast<qint64>(data.size()))return false;
    file.close();
    browser.setContentRoot(temp.path());
    return true;
}
inline bool verify(QtContentBrowser& browser) {
    auto* grid=browser.findChild<QListView*>("assetGrid");
    auto* tree=browser.findChild<QTreeWidget*>("folderTree");
    auto* detail=browser.findChild<QLabel*>("assetPreviewIcon");
    if(!grid || !grid->model() || !tree || !detail)return false;
    auto const prefix=QStringLiteral(":/northstar/icons/assetBrowser/");
    auto same=[](QIcon const& a,QIcon const& b){
        return !a.isNull() && !b.isNull() &&
            a.pixmap(128,128).toImage()==b.pixmap(128,128).toImage();
    };
    QIcon folder(prefix+"soundsFolder.png"),track(prefix+"soundFile.png");
    auto index=grid->model()->index(0,0);
    bool ok=grid->model()->rowCount()==1 &&
        index.data(Qt::DisplayRole).toString()=="vehicle.nac" &&
        same(qvariant_cast<QIcon>(index.data(Qt::DecorationRole)),folder);
    auto nodes=tree->findItems("vehicle.nac",Qt::MatchExactly|Qt::MatchRecursive,0);
    ok=ok && !nodes.isEmpty() && same(nodes.front()->icon(0),folder);
    if(ok) {
        grid->setCurrentIndex(index);
        QCoreApplication::processEvents();
        ok=detail->pixmap(Qt::ReturnByValue).toImage()==
            folder.pixmap(130,130).toImage();
    }
    if(ok) {
        browser.browseTo("vehicle.nac");
        QCoreApplication::processEvents();
        ok=grid->model()->rowCount()==2;
        for(int i=0;ok && i<grid->model()->rowCount();++i) {
            auto ix=grid->model()->index(i,0);
            grid->setCurrentIndex(ix);
            QCoreApplication::processEvents();
            ok=same(qvariant_cast<QIcon>(ix.data(Qt::DecorationRole)),track) &&
                detail->pixmap(Qt::ReturnByValue).toImage()==
                    track.pixmap(130,130).toImage() &&
                browser.selectedAsset().startsWith("vehicle.nac@track-");
        }
    }
    if(ok) {
        bool testedList=false;
        for(auto* button:browser.findChildren<QToolButton*>()) {
            if(button->toolTip()!="Toggle grid/list view")continue;
            button->click();
            testedList=true;
            ok=grid->viewMode()==QListView::ListMode;
            for(int i=0;ok && i<grid->model()->rowCount();++i)
                ok=same(qvariant_cast<QIcon>(
                    grid->model()->index(i,0).data(Qt::DecorationRole)),track);
            button->click();
            ok=ok && grid->viewMode()==QListView::IconMode;
            break;
        }
        ok=ok && testedList;
    }
    std::cout<<"NAC icons "<<(ok?"PASS":"FAIL")<<'\n';
    return ok;
}
}
