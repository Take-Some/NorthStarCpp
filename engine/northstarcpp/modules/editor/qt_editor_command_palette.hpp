#pragma once
// NorthStarCpp original Qt command palette. Reuses the QActions already
// installed by the editor: no duplicated mutation, undo, save, or play logic.
// UI/interaction pattern only, no Unreal Engine copyrighted source.
#include <QAction>
#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <vector>

namespace northstarcpp::editor {
class QtEditorCommandPalette final : public QDialog {
public:
    explicit QtEditorCommandPalette(QMainWindow* editor):QDialog(editor),
        owner_(editor) {
        setObjectName("editorCommandPalette");
        setWindowTitle("Find Editor Action");
        setModal(true);
        resize(590,420);
        setMinimumWidth(420);
        auto* root=new QVBoxLayout(this);
        root->setContentsMargins(12,12,12,12);
        auto* hint=new QLabel(
            "Search commands, panels and tools. Enter to run; Esc to close.",this);
        hint->setObjectName("commandPaletteHint");
        root->addWidget(hint);
        query_=new QLineEdit(this);
        query_->setObjectName("commandPaletteSearch");
        query_->setPlaceholderText("Type an action name (Save, Play, Focus, Content...)");
        query_->installEventFilter(this);
        root->addWidget(query_);
        results_=new QListWidget(this);
        results_->setObjectName("commandPaletteResults");
        results_->setAlternatingRowColors(true);
        root->addWidget(results_,1);
        count_=new QLabel(this);
        count_->setObjectName("commandPaletteCount");
        root->addWidget(count_);
        QSet<QAction*> seen;
        for(auto* action:owner_->findChildren<QAction*>()) {
            if(!action || seen.contains(action) || action->isSeparator()
               || action->menu() || action->text().trimmed().isEmpty())
                continue;
            seen.insert(action);
            actions_.push_back(action);
        }
        connect(query_,&QLineEdit::textChanged,this,
                [this]{repopulate();});
        connect(query_,&QLineEdit::returnPressed,this,
                [this]{executeSelected();});
        connect(results_,&QListWidget::itemActivated,this,
                [this](QListWidgetItem*){executeSelected();});
        connect(results_,&QListWidget::itemDoubleClicked,this,
                [this](QListWidgetItem*){executeSelected();});
        repopulate();
        QTimer::singleShot(0,query_,[this]{query_->setFocus();});
    }

protected:
    bool eventFilter(QObject* watched,QEvent* event) override {
        if(watched==query_ && event->type()==QEvent::KeyPress) {
            auto* key=static_cast<QKeyEvent*>(event);
            if(key->key()==Qt::Key_Down || key->key()==Qt::Key_Up) {
                if(results_->count()) {
                    int delta=key->key()==Qt::Key_Down?1:-1;
                    int next=std::clamp(results_->currentRow()+delta,
                                        0,results_->count()-1);
                    results_->setCurrentRow(next);
                }
                return true;
            }
        }
        return QDialog::eventFilter(watched,event);
    }
private:
    struct Match {int score;int index;QString title;};
    static QString titleFor(QAction* a) {
        QString title=a->text();
        title.remove('&');
        title=title.section('\t',0,0).trimmed();
        return title;
    }
    void repopulate() {
        results_->clear();
        QString needle=query_->text().trimmed();
        std::vector<Match> matches;
        for(int i=0;i<int(actions_.size());++i) {
            QAction* a=actions_[i].data();
            if(!a || !a->isEnabled() || !a->isVisible())
                continue;
            QString title=titleFor(a);
            if(title.isEmpty())continue;
            QString shortcut=a->shortcut().toString(QKeySequence::NativeText);
            bool found=needle.isEmpty() || title.contains(needle,Qt::CaseInsensitive)
                       || shortcut.contains(needle,Qt::CaseInsensitive);
            if(!found)continue;
            int score=0;
            if(!needle.isEmpty()) {
                if(title.compare(needle,Qt::CaseInsensitive)==0)score+=100;
                if(title.startsWith(needle,Qt::CaseInsensitive))score+=70;
                if(title.contains(needle,Qt::CaseInsensitive))score+=30;
                if(shortcut.contains(needle,Qt::CaseInsensitive))score+=10;
            }
            matches.push_back({score,i,title});
        }
        std::stable_sort(matches.begin(),matches.end(),
            [](Match const& a,Match const& b) {
                if(a.score!=b.score)return a.score>b.score;
                return a.title.compare(b.title,Qt::CaseInsensitive)<0;
            });
        int const visible=std::min(80,int(matches.size()));
        for(int j=0;j<visible;++j) {
            auto const& match=matches[j];
            QAction* action=actions_[match.index].data();
            auto* item=new QListWidgetItem(results_);
            QString const key=action->shortcut().toString(QKeySequence::NativeText);
            item->setText(match.title+(key.isEmpty()?QString{}:QString("    [%1]").arg(key)));
            item->setIcon(action->icon());
            item->setData(Qt::UserRole,match.index);
            item->setToolTip(action->toolTip());
        }
        if(visible)results_->setCurrentRow(0);
        count_->setText(QString("%1 available action(s)%2")
            .arg(matches.size())
            .arg(matches.size()>80?" — showing first 80":""));
    }
    void executeSelected() {
        auto* item=results_->currentItem();
        if(!item)return;
        int index=item->data(Qt::UserRole).toInt();
        if(index<0 || index>=int(actions_.size()))return;
        QPointer<QAction> action=actions_[index];
        if(!action || !action->isEnabled())return;
        accept();
        QPointer<QMainWindow> owner=owner_;
        // Execute after leaving nested dialog loop. The ACTION itself owns
        // the existing undo history, validation and enabled-state logic.
        QTimer::singleShot(0,owner.data(),[owner,action] {
            if(owner && action && action->isEnabled())action->trigger();
        });
    }
    QPointer<QMainWindow> owner_;
    QLineEdit* query_{};
    QListWidget* results_{};
    QLabel* count_{};
    std::vector<QPointer<QAction>> actions_;
};
} // namespace northstarcpp::editor
