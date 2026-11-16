#include "qt_content_browser.hpp"
#include "asset_dependency_registry.hpp"
#include "asset_browser_collections.hpp"
#include "asset_team_collections.hpp"
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QSet>
#include <algorithm>

namespace northstarcpp::editor {
namespace {
void remapCollections(QString const& root,
                      QMap<QString,QString> const& changes) {
    auto local=asset_collections::load(root);
    bool changed=false;
    for(auto it=local.begin();it!=local.end();++it) {
        for(auto& ref:it.value()) {
            if(changes.contains(ref)) {
                ref=changes.value(ref);
                changed=true;
            }
        }
    }
    if(changed)asset_collections::save(root,local);
    asset_collections::Items team;
    QString issue;
    if(asset_team_collections::load(root,team,issue)) {
        bool teamChanged=false;
        for(auto it=team.begin();it!=team.end();++it)
            for(auto& ref:it.value())
                if(changes.contains(ref)) {
                    ref=changes.value(ref);
                    teamChanged=true;
                }
        if(teamChanged)asset_team_collections::save(root,team,issue);
    }
}
}
void QtContentBrowser::showDependencyGraph() {
    auto selected=selectedPath();
    if(selected.isEmpty() || !registry_ || engine_selected_)return;
    QDialog dialog(this);
    dialog.setWindowTitle("Asset Dependencies: "+selected);
    dialog.resize(800,520);
    auto* layout=new QVBoxLayout(&dialog);
    auto* tree=new QTreeWidget(&dialog);
    tree->setObjectName("assetDependenciesTree");
    tree->setHeaderLabels({"Relationship","Asset"});
    auto const deps=registry_->dependenciesOf(selected);
    auto const referencers=registry_->referencersOf(selected);
    auto* first=new QTreeWidgetItem(tree,{
        QString("Dependencies (%1)").arg(deps.size()),selected});
    auto* second=new QTreeWidgetItem(tree,{
        QString("Referenced by (%1)").arg(referencers.size()),selected});
    for(auto const& path:deps)
        new QTreeWidgetItem(first,{"Uses",path});
    for(auto const& path:referencers)
        new QTreeWidgetItem(second,{"Used by",path});
    tree->expandAll();
    layout->addWidget(tree,1);
    auto* note=new QLabel("Double-click an indexed reference to reveal it. "
        "Opaque provider payload references are not inferred.",&dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    layout->addWidget(buttons);
    connect(tree,&QTreeWidget::itemDoubleClicked,&dialog,
            [this,&dialog](QTreeWidgetItem* item,int) {
        if(!item || !item->parent())return;
        auto path=item->text(1);
        if(path.startsWith("@project/"))return;
        if(project_catalog_.find(path.toUtf8().toStdString())) {
            dialog.accept();
            revealAsset(path);
        }
    });
    dialog.exec();
}
void QtContentBrowser::renameMoveSelected() {
    if(engine_selected_ || !registry_ || project_root_.isEmpty() ||
       !catalog_ready_) {
        QMessageBox::information(this,"Rename / Move",
            "Wait for the dependency index. Engine resources are read-only.");
        return;
    }
    auto paths=selectedPaths();
    if(paths.empty() || paths.size()>128)return;
    for(auto const& path:paths) {
        auto const* item=project_catalog_.find(path.toUtf8().toStdString());
        if(!item || item->directory || item->is_virtual_entry() ||
           item->is_list_file() || item->external_prefab) {
            QMessageBox::warning(this,"Rename / Move",
                "Select ordinary, physical project files only.");
            return;
        }
    }
    QDialog dialog(this);
    dialog.setObjectName("assetBatchMoveDialog");
    dialog.setWindowTitle("Rename / Move Assets (update references)");
    dialog.resize(700,440);
    auto* layout=new QVBoxLayout(&dialog);
    auto* form=new QFormLayout;
    auto* pattern=new QLineEdit(&dialog);
    pattern->setObjectName("assetRenamePattern");
    pattern->setText(paths.size()==1?"{name}_renamed":"{name}_{index}");
    pattern->setToolTip("Use {name} for the original stem and {index} for sequence.");
    form->addRow("New base name:",pattern);
    auto* destination=new QComboBox(&dialog);
    destination->setObjectName("assetMoveDestination");
    destination->addItem("(keep original folder)",QString{});
    QStringList folders;
    for(auto const& item:project_catalog_.entries())
        if(item.directory && !item.is_list_file() && !item.is_virtual_entry() &&
           !item.external_prefab)
            folders.append(QString::fromUtf8(item.virtual_path.c_str()));
    folders.removeDuplicates();
    folders.sort(Qt::CaseInsensitive);
    destination->addItem("(Content root)",QString{"/"});
    for(auto const& folder:folders)destination->addItem(folder,folder);
    form->addRow("Destination folder:",destination);
    layout->addLayout(form);
    auto* preview=new QTableWidget(paths.size(),2,&dialog);
    preview->setObjectName("assetMovePreview");
    preview->setHorizontalHeaderLabels({"Existing asset","New asset"});
    preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(preview,1);
    auto* message=new QLabel(&dialog);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto* buttons=new QDialogButtonBox(
        QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QVector<AssetDependencyRegistry::Move> moves;
    auto calculate=[&] {
        moves.clear();
        QSet<QString> targets;
        QString const format=pattern->text().trimmed();
        QString const folder=destination->currentData().toString();
        QString error;
        if(format.isEmpty())error="Enter a file name pattern";
        for(int i=0;i<paths.size() && error.isEmpty();++i) {
            QString const source=paths[i];
            QFileInfo info(source);
            QString base=format;
            base.replace("{name}",info.completeBaseName());
            base.replace("{index}",QString::number(i+1));
            if(!isSafeBasename(base))
                {error="Invalid filename base: "+base;break;}
            QString targetFolder=folder.isEmpty()?
                source.section('/',0,-2):folder=="/"?QString{}:folder;
            if(!source.contains('/'))targetFolder=folder.isEmpty()?
                QString{}:targetFolder;
            QString target=targetFolder.isEmpty()?base+"."+info.suffix():
                targetFolder+"/"+base+"."+info.suffix();
            if(targets.contains(target) || (source!=target &&
               project_catalog_.find(target.toUtf8().toStdString())))
                {error="Duplicate or occupied destination: "+target;break;}
            targets.insert(target);
            preview->setItem(i,0,new QTableWidgetItem(source));
            preview->setItem(i,1,new QTableWidgetItem(target));
            moves.push_back({source,target});
        }
        message->setText(error.isEmpty()?
            QString("%1 file(s) will be moved. Referencing scenes and known "
                    "text assets will be relinked transactionally.").arg(paths.size()):
            error);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(
            error.isEmpty() && moves.size()==paths.size());
    };
    connect(pattern,&QLineEdit::textChanged,&dialog,calculate);
    connect(destination,QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog,calculate);
    calculate();
    if(dialog.exec()!=QDialog::Accepted)return;
    if(moves.empty())return;
    if(mutation_preflight_ && !mutation_preflight_())return;
    if(QMessageBox::question(this,"Confirm Reference-Aware Move",
        QString("Move %1 asset(s), rewriting known scene and text references?\n"
                "Dynamic/opaque plugin references cannot be resolved automatically.")
            .arg(moves.size()),
        QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)
        return;
    auto registry=registry_;
    auto catalog=project_catalog_; // immutable worker snapshot
    QMap<QString,QString> changed;
    for(auto const& move:moves)changed.insert(move.from,move.to);
    auto root=project_root_;
    auto* watcher=new QFutureWatcher<AssetDependencyRegistry::Result>(this);
    connect(watcher,&QFutureWatcher<AssetDependencyRegistry::Result>::finished,
            this,[this,watcher,changed,root] {
        auto result=watcher->result();
        watcher->deleteLater();
        if(!result.ok) {
            QMessageBox::warning(this,"Rename / Move Failed",result.message);
            return;
        }
        remapCollections(root,changed);
        emit assetReferencesRemapped(changed);
        if(root==project_root_)refresh();
        QMessageBox::information(this,"Rename / Move Completed",result.message);
    });
    watcher->setFuture(QtConcurrent::run(
        [registry,catalog=std::move(catalog),moves]()mutable {
            return registry->moveAssets(catalog,moves);
        }));
}
}
