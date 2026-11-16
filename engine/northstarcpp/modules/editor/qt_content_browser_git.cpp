// Explicit Git actions for selected editable, physical project assets.
// No shell interpolation and no automatic add, commit, push, restore or reset.
#include "qt_content_browser.hpp"
#include "qt_text_asset_editor.hpp"
#include "qt_content_browser_internal.hpp"
#include "asset_browser_git_ops.hpp"
#include <QDialog>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QVBoxLayout>

namespace northstarcpp::editor {
namespace {
void showText(QWidget* parent,QString const& title,QString const& content) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(850,530);
    auto* root=new QVBoxLayout(&dialog);
    auto* text=text_assets::createSourceEditor(&dialog,text_assets::Format::Plain,true);
    text->setObjectName("gitOutput");
    text->setReadOnly(true);
    text->setPlainText(content);
    root->addWidget(text);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    QObject::connect(buttons,&QDialogButtonBox::rejected,
                     &dialog,&QDialog::reject);
    root->addWidget(buttons);
    dialog.exec();
}
}
void QtContentBrowser::showGitStatus() {
    if(engine_selected_ || root_.isEmpty())return;
    auto paths=selectedPaths();
    if(paths.empty())return;
    QStringList args={"status","--porcelain=v1","--untracked-files=normal","--"};
    for(auto const& path:paths) {
        auto const* rec=activeCatalog().find(path.toUtf8().toStdString());
        if(rec && !rec->is_virtual_entry() && !rec->is_list_file() &&
           !rec->external_prefab)args.append(path);
    }
    if(args.size()==4)return;

    auto* watcher=new QFutureWatcher<git_ops::Result>(this);
    connect(watcher,&QFutureWatcher<git_ops::Result>::finished,this,
            [this,watcher] {
        auto result=watcher->result();
        watcher->deleteLater();
        showText(this,"Git Status",result.output.isEmpty()?
            (result.success?"Selected assets have no changes":"Git status failed"):
            result.output);
    });
    QString const root=root_;
    watcher->setFuture(QtConcurrent::run([root,args] {
        return git_ops::run(root,args);
    }));
}
void QtContentBrowser::showGitDiff() {
    if(engine_selected_ || root_.isEmpty())return;
    auto paths=selectedPaths();
    if(paths.empty())return;
    QStringList args={"diff","--"};
    for(auto const& path:paths) {
        auto const* rec=activeCatalog().find(path.toUtf8().toStdString());
        if(rec && !rec->is_virtual_entry() && !rec->is_list_file() &&
           !rec->external_prefab)args.append(path);
    }
    if(args.size()==2)return;

    auto* watcher=new QFutureWatcher<git_ops::Result>(this);
    connect(watcher,&QFutureWatcher<git_ops::Result>::finished,this,
            [this,watcher] {
        auto result=watcher->result();
        watcher->deleteLater();
        showText(this,"Git Diff",result.output.isEmpty()?
            (result.success?"No unstaged differences":"Git diff failed"):
            result.output);
    });
    QString const root=root_;
    watcher->setFuture(QtConcurrent::run([root,args] {
        return git_ops::run(root,args);
    }));
}
void QtContentBrowser::gitStage(bool stage) {
    if(engine_selected_ || root_.isEmpty())return;
    auto paths=selectedPaths();
    if(paths.empty() || paths.size()>256)return;
    QStringList args=stage?QStringList{"add","--"}:
                            QStringList{"restore","--staged","--"};
    for(auto const& path:paths) {
        auto const* rec=activeCatalog().find(path.toUtf8().toStdString());
        if(!rec || rec->directory || rec->is_virtual_entry() ||
           rec->is_list_file() || rec->external_prefab) {
            QMessageBox::warning(this,"Git",
                "Only physical project assets can be staged or unstaged.");
            return;
        }
        args.append(path);
    }
    if(QMessageBox::question(this,stage?"Stage Assets":"Unstage Assets",
       QString("%1 %2 selected asset(s)? This does not commit or push.")
           .arg(stage?"Stage":"Unstage").arg(paths.size()),
       QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)
        return;

    auto* watcher=new QFutureWatcher<git_ops::Result>(this);
    connect(watcher,&QFutureWatcher<git_ops::Result>::finished,this,
            [this,watcher] {
        auto result=watcher->result();
        watcher->deleteLater();
        if(!result.success)
            QMessageBox::warning(this,"Git",result.output.isEmpty()?
                "Git command failed":result.output);
    });
    QString const root=root_;
    watcher->setFuture(QtConcurrent::run([root,args] {
        return git_ops::run(root,args);
    }));
}
}
