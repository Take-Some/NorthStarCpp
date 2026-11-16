#include "qt_content_browser.hpp"
#include "asset_dependency_registry.hpp"
#include "asset_browser_git_ops.hpp"
#include "qt_asset_card_delegate.hpp"
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QTimer>
#include <QLabel>
#include <QStandardItemModel>
#include <QDir>
#include <QSet>

namespace northstarcpp::editor {
void QtContentBrowser::scheduleDependencyIndex() {
    ++registry_generation_;
    if(project_root_.isEmpty() || project_catalog_.entries().empty()) {
        registry_.reset();
        return;
    }
    auto revision=registry_generation_;
    auto catalog=project_catalog_; // immutable snapshot for the background worker
    using Result=std::pair<std::shared_ptr<AssetDependencyRegistry>,QString>;
    auto* watcher=new QFutureWatcher<Result>(this);
    connect(watcher,&QFutureWatcher<Result>::finished,this,
            [this,watcher,revision] {
        try {
            auto built=watcher->result();
            if(revision==registry_generation_) {
                registry_=std::move(built.first);
                configureFileWatching();
                updateDetails();
                if(!built.second.isEmpty())
                    status_->setToolTip(built.second);
            }
        }catch(std::exception const&) {}
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&pool_,
        [catalog=std::move(catalog)]() mutable -> Result {
        auto index=std::make_shared<AssetDependencyRegistry>();
        QString warning;
        index->rebuild(catalog,warning);
        return {std::move(index),warning};
    }));
}
void QtContentBrowser::configureFileWatching() {
    if(!change_watcher_)return;
    auto old=change_watcher_->directories();
    old+=change_watcher_->files();
    if(!old.isEmpty())change_watcher_->removePaths(old);
    if(project_root_.isEmpty())return;
    QStringList paths;
    auto append=[&paths](QString const& value) {
        if(paths.size()>=768 || value.isEmpty() || paths.contains(value) ||
           !QFileInfo::exists(value))return;
        paths.append(value);
    };
    append(project_root_);
    int watchedDirs=0;
    for(auto const& entry:project_catalog_.entries()) {
        if(watchedDirs>=320 || !entry.directory || entry.is_list_file() ||
           entry.is_virtual_entry() || entry.external_prefab)continue;
        QString dir=QDir(project_root_).filePath(
            QString::fromUtf8(entry.virtual_path.c_str()));
        append(dir);
        ++watchedDirs;
    }
    if(registry_) {
        for(auto const& source:registry_->sources()) {
            append(QString::fromStdWString(source.path.parent_path().wstring()));
            if(paths.size()<768)
                append(QString::fromStdWString(source.path.wstring()));
        }
    }
    if(!paths.isEmpty())change_watcher_->addPaths(paths);
}
void QtContentBrowser::refreshGitDecorations() {
    if(project_root_.isEmpty()) {
        git_decorations_.clear();
        return;
    }
    auto root=project_root_;
    auto token=generation_;
    auto* watcher=new QFutureWatcher<QHash<QString,QString>>(this);
    connect(watcher,&QFutureWatcher<QHash<QString,QString>>::finished,
            this,[this,watcher,token,root] {
        auto status=watcher->result();
        watcher->deleteLater();
        if(token!=generation_ || project_root_!=root)return;
        git_decorations_=std::move(status);
        if(engine_selected_)return;
        for(auto it=visible_items_.begin();it!=visible_items_.end();++it) {
            auto* item=it.value();
            if(!item)continue;
            item->setData(git_decorations_.value(it.key()),
                          asset_card_roles::git_status);
        }
    });
    watcher->setFuture(QtConcurrent::run([root] {
        QHash<QString,QString> statuses;
        auto prefix=git_ops::run(root,{"rev-parse","--show-prefix"});
        auto result=git_ops::run(root,{
            "-c","core.quotePath=false","status","--porcelain=v1",
            "-z","--untracked-files=all","--","."});
        if(!prefix.success || !result.success)return statuses;
        QString pathPrefix=prefix.output.trimmed().replace('\\','/');
        auto records=result.output.split(QChar('\0'),Qt::SkipEmptyParts);
        for(int i=0;i<records.size();++i) {
            auto const& row=records[i];
            if(row.size()<4 || row[2]!=' ')continue;
            QString status=row.left(2);
            QString path=row.mid(3).replace('\\','/');
            if((status.contains('R') || status.contains('C')) &&
               i+1<records.size())++i; // porcelain -z old path follows new
            if(!pathPrefix.isEmpty() && path.startsWith(pathPrefix))
                path.remove(0,pathPrefix.size());
            if(path.startsWith("./"))path.remove(0,2);
            if(safe_virtual_path(path.toUtf8().toStdString()))
                statuses.insert(path,status);
        }
        return statuses;
    }));
}
}
