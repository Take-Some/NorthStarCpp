#pragma once
// NorthStarCpp: native, Qt-only workspace ergonomics. No Unreal Engine code.
// Install once, after QMainWindow docks, menus and original actions exist.
#include <QAction>
#include <QByteArray>
#include <QDialog>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace northstarcpp::editor::workflow {
struct ActionEntry {
    QPointer<QAction> action;
    QString category;
    QString title;
};
inline void install(QMainWindow* window,
                    QDockWidget* contentBrowser,
                    QDockWidget* outliner,
                    QDockWidget* details,
                    QWidget* viewport) {
    if(!window || window->findChild<QMenu*>("northstarUxWorkspaceMenu"))
        return;
    auto commands=std::make_shared<std::vector<ActionEntry>>();
    // Reuse all EXISTING editor actions (including save, undo, gizmos and PIE).
    std::function<void(QMenu*,QString)> scan;
    scan=[commands,&scan](QMenu* menu,QString parent) {
        if(!menu)return;
        auto category=menu->title();
        category.remove('&');
        if(!parent.isEmpty())category=parent+" / "+category;
        for(auto* action:menu->actions()) {
            if(!action || action->isSeparator())continue;
            if(auto* sub=action->menu()){scan(sub,category);continue;}
            QString name=action->text();
            name.remove('&');
            if(!name.trimmed().isEmpty())
                commands->push_back({action,category,name});
        }
    };
    for(auto* action:window->menuBar()->actions())
        if(action && action->menu())scan(action->menu(),{});

    auto* workspace=window->menuBar()->addMenu("Workspace");
    workspace->setObjectName("northstarUxWorkspaceMenu");
    auto* palette=workspace->addAction("Find Command...");
    palette->setObjectName("northstarCommandPaletteAction");
    palette->setShortcut(QKeySequence("Ctrl+Shift+P"));
    auto* drawer=workspace->addAction("Content Drawer");
    drawer->setObjectName("northstarContentDrawerAction");
    drawer->setShortcut(QKeySequence("Ctrl+Space"));
    auto* focus=workspace->addAction("Focus Viewport");
    focus->setObjectName("northstarFocusViewportAction");
    focus->setCheckable(true);
    focus->setShortcut(QKeySequence("F11"));
    for(auto* action:{palette,drawer,focus})
        commands->push_back({action,"Workspace",action->text()});

    QObject::connect(drawer,&QAction::triggered,window,[contentBrowser] {
        if(!contentBrowser)return;
        if(contentBrowser->isVisible())contentBrowser->hide();
        else {contentBrowser->show();contentBrowser->raise();}
    });

    auto previous=std::make_shared<QByteArray>();
    auto focused=std::make_shared<bool>(false);
    QObject::connect(focus,&QAction::triggered,window,
                     [window,viewport,previous,focused,focus] {
        if(!*focused) {
            *previous=window->saveState(1); // Same state version as NorthStar.
            for(auto* dock:window->findChildren<QDockWidget*>())dock->hide();
            for(auto* bar:window->findChildren<QToolBar*>())bar->hide();
            if(viewport)viewport->setFocus(Qt::OtherFocusReason);
            *focused=true;
        }else{
            *focused=false;
            if(!previous->isEmpty())window->restoreState(*previous,1);
            previous->clear();
        }
        focus->setChecked(*focused);
    });

    workspace->addSeparator();
    auto* level=workspace->addAction("Level Design Workspace");
    QObject::connect(level,&QAction::triggered,window,
                     [contentBrowser,outliner,details,focused,focus] {
        if(*focused)focus->trigger();
        for(auto* dock:{contentBrowser,outliner,details})
            if(dock)dock->show();
    });
    auto* assets=workspace->addAction("Asset Workspace");
    QObject::connect(assets,&QAction::triggered,window,
                     [contentBrowser,outliner,details,focused,focus] {
        if(*focused)focus->trigger();
        if(outliner)outliner->hide();
        if(details)details->show();
        if(contentBrowser){contentBrowser->show();contentBrowser->raise();}
    });
    commands->push_back({level,"Workspace",level->text()});
    commands->push_back({assets,"Workspace",assets->text()});

    auto* presets=workspace->addMenu("Saved Workspaces");
    auto settings=std::make_shared<QSettings>("KaylasSystems","NorthStarCppEditor");
    QObject::connect(presets,&QMenu::aboutToShow,window,
                     [presets,settings,window,focused,focus] {
        presets->clear();
        auto names=settings->value("EditorUx/LayoutNames").toStringList();
        for(auto const& name:names) {
            auto* action=presets->addAction(name);
            QObject::connect(action,&QAction::triggered,window,
                             [window,settings,name,focused,focus] {
                if(*focused)focus->trigger();
                auto state=settings->value("EditorUx/Layouts/"+name).toByteArray();
                if(!state.isEmpty())window->restoreState(state,1);
            });
        }
        if(names.isEmpty()){
            auto* empty=presets->addAction("No saved workspaces");
            empty->setEnabled(false);
        }
    });
    auto* saveLayout=workspace->addAction("Save Workspace As...");
    QObject::connect(saveLayout,&QAction::triggered,window,
                     [window,settings,focused] {
        if(*focused)return; // Never persist an empty immersive layout.
        bool accepted=false;
        auto name=QInputDialog::getText(window,"Save Workspace",
                    "Name:",QLineEdit::Normal,"Level Design",&accepted).trimmed();
        if(!accepted)return;
        if(name.isEmpty() || name.size()>48 || name.contains('/') ||
           name.contains('\\') || name.contains(':')) {
            QMessageBox::warning(window,"Workspace","Invalid workspace name.");
            return;
        }
        settings->setValue("EditorUx/Layouts/"+name,window->saveState(1));
        auto names=settings->value("EditorUx/LayoutNames").toStringList();
        if(!names.contains(name))names.append(name);
        names.sort(Qt::CaseInsensitive);
        settings->setValue("EditorUx/LayoutNames",names);
        settings->sync();
    });
    auto startup=std::make_shared<QByteArray>(window->saveState(1));
    auto* reset=workspace->addAction("Reset to Startup Layout");
    QObject::connect(reset,&QAction::triggered,window,
                     [window,startup,focused,focus] {
        if(*focused)focus->trigger();
        if(!startup->isEmpty())window->restoreState(*startup,1);
    });
    commands->push_back({saveLayout,"Workspace",saveLayout->text()});
    commands->push_back({reset,"Workspace",reset->text()});
    workspace->addSeparator();
    for(auto* dock:{outliner,details,contentBrowser})
        if(dock)workspace->addAction(dock->toggleViewAction());

    auto* dialog=new QDialog(window);
    dialog->setWindowTitle("NorthStar - Quick Actions");
    dialog->setObjectName("northstarUxCommandPalette");
    dialog->resize(610,430);
    auto* vertical=new QVBoxLayout(dialog);
    auto* hint=new QLabel("Search actions  |  Enter: run  |  Esc: close",dialog);
    vertical->addWidget(hint);
    auto* query=new QLineEdit(dialog);
    query->setPlaceholderText("Search commands, tools and panels...");
    query->setAccessibleName("Search editor commands");
    vertical->addWidget(query);
    auto* results=new QListWidget(dialog);
    results->setUniformItemSizes(true);
    results->setAccessibleName("Matching commands");
    vertical->addWidget(results);
    auto recent=std::make_shared<QStringList>(
        settings->value("EditorUx/RecentCommands").toStringList());
    auto refresh=[query,results,commands,recent] {
        QString const token=query->text().trimmed().toCaseFolded();
        struct Hit{int index,rank;};
        std::vector<Hit> hits;
        for(int i=0;i<static_cast<int>(commands->size());++i) {
            auto const& entry=commands->at(static_cast<std::size_t>(i));
            if(!entry.action || !entry.action->isEnabled())continue;
            auto const label=entry.title.toCaseFolded();
            auto const category=entry.category.toCaseFolded();
            int rank=-1;
            if(token.isEmpty())rank=1;
            else if(label.startsWith(token))rank=300;
            else if(label.contains(token))rank=230;
            else if(category.contains(token))rank=100;
            if(rank<0)continue;
            int const pos=recent->indexOf(entry.action->objectName());
            if(pos>=0)rank+=std::max(0,65-pos*7);
            hits.push_back({i,rank});
        }
        std::stable_sort(hits.begin(),hits.end(),
            [](Hit const& a,Hit const& b){return a.rank>b.rank;});
        results->clear();
        for(auto const& hit:hits) {
            if(results->count()>=50)break;
            auto const& entry=commands->at(static_cast<std::size_t>(hit.index));
            QString const shortcut=entry.action->shortcut().toString();
            QString const title=entry.title+"  |  "+entry.category+
                  (shortcut.isEmpty()?QString{}:"  |  "+shortcut);
            auto* item=new QListWidgetItem(title,results);
            item->setData(Qt::UserRole,hit.index);
        }
        if(results->count())results->setCurrentRow(0);
    };
    auto execute=[dialog,results,commands,recent,settings] {
        if(!results->currentItem())return;
        int const idx=results->currentItem()->data(Qt::UserRole).toInt();
        if(idx<0 || idx>=static_cast<int>(commands->size()))return;
        auto action=commands->at(static_cast<std::size_t>(idx)).action;
        if(!action || !action->isEnabled())return;
        auto id=action->objectName();
        if(id.isEmpty())id=commands->at(static_cast<std::size_t>(idx)).category+
                            "/"+commands->at(static_cast<std::size_t>(idx)).title;
        recent->removeAll(id);
        recent->prepend(id);
        while(recent->size()>12)recent->removeLast();
        settings->setValue("EditorUx/RecentCommands",*recent);
        dialog->hide();
        action->trigger(); // Execute the real NorthStar QAction.
    };
    QObject::connect(query,&QLineEdit::textChanged,dialog,refresh);
    QObject::connect(query,&QLineEdit::returnPressed,dialog,execute);
    QObject::connect(results,&QListWidget::itemDoubleClicked,dialog,
                     [execute](QListWidgetItem*){execute();});
    auto* down=new QShortcut(QKeySequence(Qt::Key_Down),dialog);
    auto* up=new QShortcut(QKeySequence(Qt::Key_Up),dialog);
    QObject::connect(down,&QShortcut::activated,dialog,[results] {
        results->setCurrentRow(std::min(results->count()-1,
                                       results->currentRow()+1));
    });
    QObject::connect(up,&QShortcut::activated,dialog,[results] {
        results->setCurrentRow(std::max(0,results->currentRow()-1));
    });
    QObject::connect(palette,&QAction::triggered,window,
                     [dialog,query,refresh] {
        query->clear();
        refresh();
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        query->setFocus(Qt::ShortcutFocusReason);
    });
    // Compact discoverable search button on the EXISTING toolbar.
    if(auto* bar=window->findChild<QToolBar*>("authoringToolbar")) {
        bar->addSeparator();
        auto* quick=bar->addAction("Commands");
        quick->setToolTip("Find any editor command (Ctrl+Shift+P)");
        QObject::connect(quick,&QAction::triggered,palette,&QAction::trigger);
    }
}
} // namespace northstarcpp::editor::workflow
