#pragma once
// Original NorthStarCpp keyboard-shortcut editor, not UE code.
// Controls the *existing* QActions. Unsupported/unnamed actions are not
// modified; overrides are persisted under NorthStarQtEditor QSettings.
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHash>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVector>
#include <algorithm>

namespace northstarcpp::editor::shortcuts {
struct Entry {
    QString id;
    QString label;
    QAction* action{};
    QKeySequence original;
    QKeySequence value;
};
inline QString title(QAction const* action) {
    QString text=action->text();
    text.remove('&');
    return text.section('\t',0,0).trimmed();
}
inline QVector<Entry> collect(QMainWindow* window) {
    QVector<Entry> entries;
    QHash<QString,int> occurrences;
    for(auto* action:window->findChildren<QAction*>()) {
        if(!action || action->isSeparator() || action->menu() ||
           action->objectName().isEmpty() || title(action).isEmpty())
            continue;
        occurrences[action->objectName()]++;
    }
    for(auto* action:window->findChildren<QAction*>()) {
        if(!action || action->isSeparator() || action->menu() ||
           action->objectName().isEmpty() || title(action).isEmpty() ||
           occurrences.value(action->objectName())!=1)
            continue;
        if(!action->property("northstarDefaultShortcut").isValid())
            action->setProperty("northstarDefaultShortcut",
                action->shortcut().toString(QKeySequence::PortableText));
        QString const source=action->property(
            "northstarDefaultShortcut").toString();
        entries.append({action->objectName(),title(action),action,
            QKeySequence::fromString(source,QKeySequence::PortableText),
            action->shortcut()});
    }
    return entries;
}
inline bool isConflict(QMainWindow* window,QVector<Entry> const& entries,
                       int current,QKeySequence candidate,
                       QString* description) {
    if(candidate.isEmpty())return false;
    // Respect all other editor shortcuts, including unnamed actions
    // inaccessible from the customization UI.
    for(auto* action:window->findChildren<QAction*>()) {
        if(!action || action->isSeparator() || action->menu())continue;
        int matched=-1;
        for(int i=0;i<entries.size();++i)
            if(entries[i].action==action){matched=i;break;}
        if(matched==current)continue;
        auto const other=matched>=0?entries[matched].value:action->shortcut();
        // Qt also treats a single-stroke shortcut as ambiguous with a
        // multi-stroke shortcut sharing its prefix. Reject both directions.
        if(!other.isEmpty() &&
           (candidate.matches(other)!=QKeySequence::NoMatch ||
            other.matches(candidate)!=QKeySequence::NoMatch)) {
            if(description)*description=title(action);
            return true;
        }
    }
    return false;
}
inline void restore(QMainWindow* window) {
    auto entries=collect(window);
    QSettings settings("KaylasSystems","NorthStarQtEditor");
    for(auto& entry:entries) {
        QString const key="Shortcuts/"+entry.id;
        if(!settings.contains(key))continue;
        auto const stored=settings.value(key).toString();
        auto const custom=QKeySequence::fromString(stored,QKeySequence::PortableText);
        if(stored.isEmpty() || !custom.isEmpty())entry.value=custom;
    }
    // Resolve the stored configuration as a whole. A binding may reuse
    // a default key that a different command explicitly released.
    bool resolved;
    do {
        resolved=true;
        for(int i=0;i<entries.size();++i) {
            auto& entry=entries[i];
            if(entry.value!=entry.original && isConflict(window,entries,i,entry.value,nullptr)) {
                entry.value=entry.original;
                resolved=false;
            }
        }
    }while(!resolved);
    for(auto const& entry:entries)
        if(entry.value!=entry.original)entry.action->setShortcut(entry.value);
}
inline void openEditor(QMainWindow* window) {
    auto entries=collect(window);
    QDialog dialog(window);
    dialog.setWindowTitle("Keyboard Shortcuts");
    dialog.setObjectName("keyboardShortcutsDialog");
    dialog.resize(750,560);
    auto* root=new QVBoxLayout(&dialog);
    auto* guidance=new QLabel(
        "Change an existing editor command shortcut. Conflicting bindings are rejected.",
        &dialog);
    guidance->setWordWrap(true);
    root->addWidget(guidance);
    auto* search=new QLineEdit(&dialog);
    search->setObjectName("shortcutSearch");
    search->setPlaceholderText("Filter commands...");
    search->setClearButtonEnabled(true);
    root->addWidget(search);
    auto* tree=new QTreeWidget(&dialog);
    tree->setObjectName("shortcutActionTree");
    tree->setColumnCount(3);
    tree->setHeaderLabels({"Command","Shortcut","Action ID"});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    root->addWidget(tree,1);
    auto* keyRow=new QHBoxLayout;
    keyRow->addWidget(new QLabel("New shortcut:",&dialog));
    auto* sequence=new QKeySequenceEdit(&dialog);
    sequence->setObjectName("shortcutSequenceInput");
    keyRow->addWidget(sequence,1);
    auto* set=new QPushButton("Assign",&dialog);
    set->setObjectName("assignShortcut");
    keyRow->addWidget(set);
    auto* clear=new QPushButton("Unbind",&dialog);
    clear->setObjectName("unbindShortcut");
    keyRow->addWidget(clear);
    auto* reset=new QPushButton("Reset Selected",&dialog);
    reset->setObjectName("resetSelectedShortcut");
    keyRow->addWidget(reset);
    root->addLayout(keyRow);
    auto* resetAll=new QPushButton("Reset All to Defaults",&dialog);
    resetAll->setObjectName("resetAllShortcuts");
    root->addWidget(resetAll);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|
                                       QDialogButtonBox::Cancel,&dialog);
    root->addWidget(buttons);
    for(int i=0;i<entries.size();++i) {
        auto const& entry=entries[i];
        auto* item=new QTreeWidgetItem(tree);
        item->setText(0,entry.label);
        item->setText(1,entry.value.toString(QKeySequence::NativeText));
        item->setText(2,entry.id);
        item->setData(0,Qt::UserRole,i);
    }
    tree->resizeColumnToContents(0);
    tree->resizeColumnToContents(1);
    auto selectedIndex=[tree]() -> int {
        auto* item=tree->currentItem();
        return item?item->data(0,Qt::UserRole).toInt():-1;
    };
    QObject::connect(search,&QLineEdit::textChanged,&dialog,
        [tree](QString const& text) {
            QString const match=text.trimmed();
            for(int i=0;i<tree->topLevelItemCount();++i) {
                auto* item=tree->topLevelItem(i);
                item->setHidden(!match.isEmpty() &&
                    !item->text(0).contains(match,Qt::CaseInsensitive) &&
                    !item->text(1).contains(match,Qt::CaseInsensitive) &&
                    !item->text(2).contains(match,Qt::CaseInsensitive));
            }
        });
    QObject::connect(tree,&QTreeWidget::currentItemChanged,&dialog,
        [sequence,&entries](QTreeWidgetItem* item,QTreeWidgetItem*) {
            QSignalBlocker blocked(sequence);
            int i=item?item->data(0,Qt::UserRole).toInt():-1;
            sequence->setKeySequence(i>=0 && i<entries.size()?
                entries[i].value:QKeySequence{});
        });
    auto redraw=[tree,&entries] {
        for(int i=0;i<tree->topLevelItemCount();++i) {
            auto* item=tree->topLevelItem(i);
            int idx=item->data(0,Qt::UserRole).toInt();
            item->setText(1,entries[idx].value.toString(
                QKeySequence::NativeText));
        }
    };
    QObject::connect(set,&QPushButton::clicked,&dialog,
        [window,&entries,sequence,selectedIndex,redraw,&dialog] {
            int const i=selectedIndex();
            if(i<0 || i>=entries.size())return;
            auto const shortcut=sequence->keySequence();
            QString conflicted;
            if(isConflict(window,entries,i,shortcut,&conflicted)) {
                QMessageBox::warning(&dialog,"Shortcut Conflict",
                    "This shortcut is already used by: "+conflicted);
                return;
            }
            entries[i].value=shortcut;redraw();
        });
    QObject::connect(clear,&QPushButton::clicked,&dialog,
        [&entries,selectedIndex,sequence,redraw] {
            int const i=selectedIndex();
            if(i<0 || i>=entries.size())return;
            entries[i].value=QKeySequence{};
            sequence->clear();
            redraw();
        });
    QObject::connect(reset,&QPushButton::clicked,&dialog,
        [&entries,selectedIndex,sequence,redraw] {
            int const i=selectedIndex();
            if(i<0 || i>=entries.size())return;
            entries[i].value=entries[i].original;
            sequence->setKeySequence(entries[i].value);
            redraw();
        });
    QObject::connect(resetAll,&QPushButton::clicked,&dialog,
        [&entries,sequence,selectedIndex,redraw] {
            for(auto& entry:entries)entry.value=entry.original;
            int i=selectedIndex();
            if(i>=0 && i<entries.size())
                sequence->setKeySequence(entries[i].value);
            redraw();
        });
    QObject::connect(buttons,&QDialogButtonBox::rejected,
                     &dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,
        [window,&dialog,&entries] {
            // Validate the WHOLE set, not just the last Assign button.
            for(int i=0;i<entries.size();++i) {
                QString description;
                if(entries[i].value!=entries[i].original &&
                   isConflict(window,entries,i,entries[i].value,&description)) {
                    QMessageBox::warning(&dialog,"Shortcut Conflict",
                        entries[i].label+" conflicts with "+description);
                    return;
                }
            }
            QSettings settings("KaylasSystems","NorthStarQtEditor");
            for(auto const& entry:entries) {
                QString const key="Shortcuts/"+entry.id;
                if(entry.value==entry.original)settings.remove(key);
                else settings.setValue(key,
                    entry.value.toString(QKeySequence::PortableText));
                entry.action->setShortcut(entry.value);
            }
            settings.sync();
            dialog.accept();
        });
    if(tree->topLevelItemCount())
        tree->setCurrentItem(tree->topLevelItem(0));
    dialog.exec();
}
} // namespace northstarcpp::editor::shortcuts
