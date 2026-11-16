#pragma once
// Native Qt dock acceptance: selecting large metadata cannot resize the dock.
// The fixture is authored in this smoke, not copied from Unreal Engine.
#include "qt_content_browser.hpp"
#include "asset_browser_collections.hpp"
#include <QComboBox>
#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QSettings>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDir>
#include <QShortcut>
#include <QTreeWidget>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QListView>
#include <QMainWindow>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

namespace northstarcpp::editor::browser_layout_qa {
inline bool prepare(QTemporaryDir& temp,QtContentBrowser& browser) {
    if(!temp.isValid() || !QDir().mkpath(temp.path()+"/props"))
        return false;
    QFile first(temp.path()+"/alpha.unknown");
    if(!first.open(QIODevice::WriteOnly))return false;
    first.write("small");
    first.close();
    QFile second(temp.path()+"/very_long_material_function_name_for_selection.material-function.json");
    if(!second.open(QIODevice::WriteOnly))return false;
    QByteArray data=R"({"schema":"northstar.material-function.v1","inputs":[)";
    for(int i=0;i<100;++i) {
        if(i)data+=",";
        data+="{\"name\":\"ExtremelyLongMaterialParameter";
        data+=QByteArray::number(i);
        data+="\",\"type\":\"float4\"}";
    }
    data+="],\"outputs\":[],\"nodes\":[]}";
    bool const okay=second.write(data)==data.size();
    second.close();
    if(okay) {
        asset_collections::Items named;
        named.insert("Review",QStringList{"alpha.unknown"});
        asset_collections::save(temp.path(),named);
        browser.setContentRoot(temp.path());
    }
    return okay;
}
inline bool verify(QMainWindow& window,QtContentBrowser& browser,
                   QDockWidget& dock) {
    auto* grid=browser.findChild<QListView*>("assetGrid");
    auto* scroll=browser.findChild<QScrollArea*>("assetDetailsScroll");
    auto* info=browser.findChild<QLabel*>("previewMetadata");
    auto* sort=browser.findChild<QComboBox*>("assetSortOrder");
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<9000 && browser.recordCount()<3) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    if(!grid || !grid->model() || !scroll || !info || !sort ||
       grid->model()->rowCount()!=3)return false;
    window.resize(1250,840);
    QCoreApplication::processEvents();
    auto choose=[&](QString const& filename) {
        for(int n=0;n<grid->model()->rowCount();++n) {
            auto index=grid->model()->index(n,0);
            if(index.data(Qt::DisplayRole).toString()==filename) {
                grid->setCurrentIndex(index);
                QCoreApplication::processEvents();
                return true;
            }
        }
        return false;
    };
    if(!choose("alpha.unknown"))return false;
    auto const h=dock.height();
    auto const browserHeight=browser.height();
    if(h<160 || browserHeight<130)return false;
    bool okay=choose(
        "very_long_material_function_name_for_selection.material-function.json");
    okay=okay && info->text().contains("ExtremelyLongMaterialParameter99") &&
         scroll->verticalScrollBar()->maximum()>0 &&
         dock.height()==h && browser.height()==browserHeight;
    if(okay) {
        sort->setCurrentIndex(1);
        QCoreApplication::processEvents();
        okay=dock.height()==h && browser.height()==browserHeight &&
             choose("alpha.unknown") &&
             dock.height()==h && browser.height()==browserHeight;
    }
    if(okay) {
        sort->setCurrentIndex(0);
        QCoreApplication::processEvents();
        okay=grid->model()->index(0,0).data(Qt::DisplayRole).toString()=="props" &&
             grid->model()->index(1,0).data(Qt::DisplayRole).toString()=="alpha.unknown";
        sort->setCurrentIndex(1);
        QCoreApplication::processEvents();
        okay=okay && grid->model()->index(1,0).data(Qt::DisplayRole)
            .toString().startsWith("very_long_material");
    }
    if(okay) {
        browser.setSearchText("type:material name:very_long");
        QCoreApplication::processEvents();
        okay=browser.visibleCount()==1 &&
             grid->model()->index(0,0).data(Qt::DisplayRole)
                .toString().startsWith("very_long_material");
        browser.setSearchText("name:alpha type:other");
        QCoreApplication::processEvents();
        okay=okay && browser.visibleCount()==1 &&
             grid->model()->index(0,0).data(Qt::DisplayRole)
                 .toString()=="alpha.unknown";
        browser.setSearchText({});
        QCoreApplication::processEvents();
    }
    if(okay) {
        // Real Ctrl+D shortcut invokes the private favorites action.
        okay=choose("props");
        QShortcut* favorite=nullptr;
        for(auto* shortcut:browser.findChildren<QShortcut*>())
            if(shortcut->key()==QKeySequence("Ctrl+D"))
                favorite=shortcut;
        auto* tree=browser.findChild<QTreeWidget*>("folderTree");
        okay=okay && favorite && tree &&
             QMetaObject::invokeMethod(favorite,"activated",Qt::DirectConnection);
        auto* favorites=tree?tree->topLevelItem(2):nullptr;
        okay=okay && favorites && favorites->childCount()==1 &&
             favorites->child(0)->text(0)=="Project: props" &&
             dock.height()==h;
        if(favorite)QMetaObject::invokeMethod(
            favorite,"activated",Qt::DirectConnection);
    }
    if(okay) {
        browser.setSearchText("(ext:unknown OR ext:ybn) AND NOT name:missing");
        QCoreApplication::processEvents();
        okay=browser.visibleCount()==1;
        browser.setSearchText({});
        QCoreApplication::processEvents();
    }
    if(okay) {
        browser.setActiveCollection("Review");
        QCoreApplication::processEvents();
        okay=browser.visibleCount()==1 &&
             grid->model()->index(0,0).data(Qt::DisplayRole)
                .toString()=="alpha.unknown";
        browser.setActiveCollection({});
        QCoreApplication::processEvents();
        okay=okay && browser.visibleCount()==3;
    }
    if(okay) {
        auto* selection=grid->selectionModel();
        selection->clearSelection();
        for(int row=0;row<grid->model()->rowCount();++row) {
            auto index=grid->model()->index(row,0);
            if(index.data(Qt::DisplayRole).toString().endsWith(".unknown") ||
               index.data(Qt::DisplayRole).toString().endsWith(".json"))
                selection->select(index,QItemSelectionModel::Select);
        }
        QShortcut* copy=nullptr;
        for(auto* shortcut:browser.findChildren<QShortcut*>())
            if(shortcut->key()==QKeySequence::Copy)copy=shortcut;
        okay=copy && QMetaObject::invokeMethod(
            copy,"activated",Qt::DirectConnection);
        auto const copied=QApplication::clipboard()->text();
        okay=okay && copied.contains("alpha.unknown") &&
             copied.contains(".json") && copied.contains('\n');
    }
    {
        QSettings settings("Kayla's Systems","NorthStarCpp");
        settings.remove(asset_collections::storageKey(browser.contentRoot()));
    }
    okay=okay && dock.height()==h && browser.height()==browserHeight;
    std::cout<<"Content Browser stable dock, sort, search, favorites: "
             <<(okay?"PASS":"FAIL")<<" ("<<h<<"px)\n";
    return okay;
}
}
