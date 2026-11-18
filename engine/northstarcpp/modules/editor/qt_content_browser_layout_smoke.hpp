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
    // Regression: saving/deleting files must not send navigation to /.
    // Exercise the exact asynchronous refresh triggered by file watching.
    if(okay) {
        auto const nested=browser.contentRoot()+"/props/inside";
        okay=QDir().mkpath(nested);
        if(okay) {
            for(int i=0;i<90;++i) {
                QFile item(nested+QString("/asset_%1.txt").arg(i,3,10,QChar('0')));
                if(!item.open(QIODevice::WriteOnly)) {okay=false;break;}
                item.write("stable content");
            }
        }
        auto awaitReady=[&](int expectedMinimum) {
            QElapsedTimer clock;clock.start();
            while(clock.elapsed()<9000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                if(browser.catalogReady() && browser.visibleCount()>=expectedMinimum)
                    return true;
                QThread::msleep(10);
            }
            return false;
        };
        auto refreshAndWait=[&](int minimum) {
            browser.refresh();
            // No intermediate root navigation, even before indexing finishes.
            if(browser.currentFolder()!="props/inside")return false;
            return awaitReady(minimum);
        };
        if(okay) {
            browser.refresh();
            okay=awaitReady(3);
        }
        if(okay) {
            browser.browseTo("props/inside");
            okay=browser.currentFolder()=="props/inside" && browser.visibleCount()==90;
        }
        if(okay) {
            auto* items=grid->model();
            for(int i=0;i<items->rowCount();++i) {
                auto index=items->index(i,0);
                if(index.data(Qt::DisplayRole).toString()=="asset_042.txt") {
                    grid->setCurrentIndex(index);
                    break;
                }
            }
            okay=browser.currentSelection()=="props/inside/asset_042.txt";
            // Intentionally scroll away from the selected item. Refresh must
            // retain scroll, not scroll to a new topmost card.
            auto* bar=grid->verticalScrollBar();
            int const target=std::min(6,bar->maximum());
            bar->setValue(target);
            okay=okay && refreshAndWait(90);
            QCoreApplication::processEvents();
            okay=okay && browser.currentFolder()=="props/inside" &&
                 browser.currentSelection()=="props/inside/asset_042.txt" &&
                 bar->value()==target;
            if(okay) {
                QFile changed(nested+"/asset_042.txt");
                okay=changed.open(QIODevice::Append);
                if(okay) {
                    changed.write(" after save");
                    changed.close();
                    browser.refresh();
                    browser.refresh(); // watcher burst / overlapping scans
                    okay=awaitReady(90) &&
                        browser.currentFolder()=="props/inside" &&
                        browser.currentSelection()=="props/inside/asset_042.txt";
                }
            }
            if(okay) {
                okay=QFile::remove(nested+"/asset_042.txt") &&
                    refreshAndWait(89);
                okay=okay && browser.currentFolder()=="props/inside" &&
                     browser.currentSelection().isEmpty();
            }
        }
        if(okay) {
            // If the folder itself disappears, use its existing parent.
            okay=QDir(nested).removeRecursively();
            if(okay) {
                browser.refresh();
                QElapsedTimer clock;clock.start();
                while(clock.elapsed()<9000 && !browser.catalogReady()) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                    QThread::msleep(10);
                }
                okay=browser.catalogReady() &&
                    browser.currentFolder()=="props";
            }
        }
        std::cout<<"Content Browser preserve folder/selection/scroll on save/delete: "
                 <<(okay?"PASS":"FAIL")<<"\n";
    }
    std::cout<<"Content Browser stable dock, sort, search, favorites: "
             <<(okay?"PASS":"FAIL")<<" ("<<h<<"px)\n";
    return okay;
}
}
