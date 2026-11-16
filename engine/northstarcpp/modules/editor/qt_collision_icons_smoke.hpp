#pragma once
// Qt offscreen asset-browser regression for the new YBN/collision artwork.
#include "qt_content_browser.hpp"
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLabel>
#include <QListView>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <iostream>

namespace northstarcpp::editor::collision_browser_qa {
inline bool prepare(QTemporaryDir& temp, QtContentBrowser& browser) {
    if(!temp.isValid() || !QDir(temp.path()).mkpath("collisions"))return false;
    // No dependency on a proprietary asset file for the artwork QA:
    // invalid YBN content must remain visible as a Collision file.
    QFile file(temp.path()+"/collisions/prop_barier_conc_01a.ybn");
    if(!file.open(QIODevice::WriteOnly) ||
       file.write("NEF8_INVALID_FIXTURE",20)!=20)return false;
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
    QIcon const directory(prefix+"collisionDir.png");
    QIcon const file(prefix+"collisionFile.png");
    auto const same=[](QIcon const& a,QIcon const& b) {
        return !a.isNull() && !b.isNull() &&
               a.pixmap(128,128).toImage()==b.pixmap(128,128).toImage();
    };
    auto get=[](QModelIndex const& index) {
        return qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    };
    QCoreApplication::processEvents();
    auto folderIndex=grid->model()->index(0,0);
    bool ok=grid->model()->rowCount()==1 &&
            folderIndex.data(Qt::DisplayRole).toString()=="collisions" &&
            same(get(folderIndex),directory);
    auto const treeItems=tree->findItems("collisions",
        Qt::MatchExactly|Qt::MatchRecursive,0);
    ok=ok && !treeItems.isEmpty() && same(treeItems.front()->icon(0),directory);
    if(ok) {
        grid->setCurrentIndex(folderIndex);
        QCoreApplication::processEvents();
        ok=detail->pixmap(Qt::ReturnByValue).toImage()==
           directory.pixmap(130,130).toImage();
    }
    if(ok) {
        browser.browseTo("collisions");
        QCoreApplication::processEvents();
        auto index=grid->model()->index(0,0);
        ok=grid->model()->rowCount()==1 &&
           index.data(Qt::DisplayRole).toString()=="prop_barier_conc_01a.ybn" &&
           same(get(index),file);
        if(ok) {
            grid->setCurrentIndex(index);
            QCoreApplication::processEvents();
            ok=detail->pixmap(Qt::ReturnByValue).toImage()==
               file.pixmap(130,130).toImage() &&
               browser.selectedAsset()=="collisions/prop_barier_conc_01a.ybn";
        }
        if(ok) {
            bool toggled=false;
            for(auto* button:browser.findChildren<QToolButton*>()) {
                if(button->toolTip()!="Toggle grid/list view")continue;
                button->click();
                toggled=true;
                ok=grid->viewMode()==QListView::ListMode &&
                   same(get(grid->model()->index(0,0)),file);
                button->click();
                ok=ok && grid->viewMode()==QListView::IconMode;
                break;
            }
            ok=ok && toggled;
        }
    }
    std::cout<<"YBN collision folder/file icons: "<<(ok?"PASS":"FAIL")<<'\n';
    return ok;
}
}
