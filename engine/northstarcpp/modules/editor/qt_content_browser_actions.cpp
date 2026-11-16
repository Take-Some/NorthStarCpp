// Isolated Content Browser file operations and context menus.
// Mutations remain project-only; read-only engine/NEF8 references are guarded.
#include "qt_content_browser.hpp"
#include "qt_content_browser_internal.hpp"
#include "qt_content_browser_favorites.hpp"
#include "asset_browser_collections.hpp"
#include "asset_team_collections.hpp"
#include <northstarcpp/scene.hpp>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QUrl>
#include <filesystem>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <vector>
#include <stdexcept>

namespace northstarcpp::editor {
namespace fs=std::filesystem;
using browser_internal::qt;
using browser_internal::native;
bool QtContentBrowser::isSafeBasename(QString const& name) const {
    if(name.isEmpty() || name=="." || name==".." || name.size()>120 ||
       name.contains('/') || name.contains('\\') || name.contains(':') ||
       name.contains(QChar('\0')))return false;
    return true;
}
void QtContentBrowser::createFolder() {
    // Engine shared content is a read-only authoring mount. Use a
    // project override instead of mutating the engine installation.
    if(engine_selected_)return;
    if(root_.isEmpty())return;
    if(auto const* folder=activeCatalog().find(folder_.toUtf8().toStdString());
       folder && folder->is_list_file()) {
        QMessageBox::information(this,"NEF8 ListFile",
            "Dictionary entries are read-only. Edit the source asset with an importer.");
        return;
    }
    bool accepted=false;
    QString name=QInputDialog::getText(this,"New Content Folder",
        "Folder name:",QLineEdit::Normal,"NewFolder",&accepted).trimmed();
    if(!accepted)return;
    if(!isSafeBasename(name)) {
        QMessageBox::warning(this,"Invalid Folder","Use a safe folder name.");return;
    }
    QString target;
    try {
        auto parent=folder_.isEmpty()?native(root_):
            activeCatalog().resolve(folder_.toUtf8().toStdString());
        target=qt(parent/native(name));
    }catch(std::exception const& e) {
        QMessageBox::warning(this,"New Folder",QString::fromUtf8(e.what()));
        return;
    }
    if(!QDir().mkpath(target))
        QMessageBox::warning(this,"Folder","Unable to create this folder.");
    else refresh();
}
void QtContentBrowser::importFiles() {
    // Engine shared content is a read-only authoring mount. Use a
    // project override instead of mutating the engine installation.
    if(engine_selected_)return;
    if(root_.isEmpty())return;
    if(auto const* folder=activeCatalog().find(folder_.toUtf8().toStdString());
       folder && folder->is_list_file()) {
        QMessageBox::information(this,"NEF8 ListFile",
            "Dictionary entries are read-only. Edit the source asset with an importer.");
        return;
    }
    QFileDialog dialog(this,"Import Project Assets");
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setOption(QFileDialog::DontUseNativeDialog,true);
    if(dialog.exec()!=QDialog::Accepted)return;
    int imported=0;
    for(auto const& source:dialog.selectedFiles()) {
        auto file=QFileInfo(source);
        if(!file.isFile() || file.size()>512ll*1024*1024)continue;
        QString dest;
        try {
            auto parent=folder_.isEmpty()?native(root_):
                activeCatalog().resolve(folder_.toUtf8().toStdString());
            dest=qt(parent/native(file.fileName()));
        }catch(std::exception const& e) {
            QMessageBox::warning(this,"Import Files",QString::fromUtf8(e.what()));
            break;
        }
        if(QFileInfo::exists(dest)) {
            auto choice=QMessageBox::question(this,"Existing Asset",
                "The asset "+file.fileName()+" already exists. Skip it?",
                QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Yes);
            if(choice==QMessageBox::Cancel)break;
            continue;
        }
        if(QFile::copy(source,dest))++imported;
    }
    if(imported)refresh();
}
void QtContentBrowser::createNativeModelPrefab() {
    if(engine_selected_ || project_root_.isEmpty())return;
    const auto selected=selectedPath();
    auto const* record=activeCatalog().find(selected.toUtf8().toStdString());
    if(!record || !record->directory || record->is_list_file())return;
    try {
        namespace fs=std::filesystem;
        auto const assetRoot=fs::canonical(native(project_root_));
        auto const folder=fs::canonical(activeCatalog().resolve(selected.toUtf8().toStdString()));
        if(folder==assetRoot || !fs::is_directory(folder))
            throw std::invalid_argument("Select a folder containing native models.");
        auto const relative=folder.lexically_relative(assetRoot);
        if(relative.empty() || *relative.begin()!="models" ||
           std::any_of(relative.begin(),relative.end(),[](fs::path const& p) {
               return p=="." || p=="..";
           }))
            throw std::invalid_argument("Select a project folder under assets/models.");
        std::vector<fs::path> parts;
        std::size_t ignored=0;
        for(auto const& entry:fs::directory_iterator(folder)) {
            if(entry.is_symlink() || !entry.is_regular_file())continue;
            auto const path=entry.path();
            auto extension=path.extension().u8string();
            std::transform(extension.begin(),extension.end(),extension.begin(),
                [](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(extension!=".ydr" && extension!=".ydd")continue;
            auto name=path.filename().u8string();
            std::transform(name.begin(),name.end(),name.begin(),
                [](unsigned char c){return static_cast<char>(std::tolower(c));});
            // These meshes are for physics/shadow passes, not visible model pieces.
            if(name.find("collider")!=std::string::npos ||
               name.find("shadow")!=std::string::npos) {++ignored;continue;}
            if(fs::file_size(path)>64u*1024u*1024u) {++ignored;continue;}
            std::ifstream input(path,std::ios::binary);
            std::array<char,4> magic{};
            input.read(magic.data(),static_cast<std::streamsize>(magic.size()));
            if(!input || magic!=std::array<char,4>{'N','E','F','8'}) {
                ++ignored;
                continue;
            }
            parts.push_back(path);
        }
        if(parts.empty())
            throw std::runtime_error("No NEF8 YDD/YDR renderable models found.");
        if(parts.size()>128)
            throw std::length_error("A composite native model may contain at most 128 parts.");
        std::sort(parts.begin(),parts.end());
        // Make the visible vehicle body the prefab root where one is available.
        auto const body=std::find_if(parts.begin(),parts.end(),[](fs::path const& path){
            auto name=path.filename().u8string();
            return name.find("body_01.ydr")!=std::string::npos;
        });
        if(body!=parts.end())std::rotate(parts.begin(),body,body+1);
        Scene model;
        model.version=3;
        model.name=folder.filename().u8string();
        model.layers.push_back({"Default",true});
        for(auto const& part:parts) {
            SceneObject object;
            object.name=part.stem().u8string();
            object.node.id=static_cast<std::uint64_t>(model.objects.size()+1);
            object.node.parent=model.objects.empty()?0u:1u;
            object.node.asset=part.lexically_relative(assetRoot).generic_u8string();
            object.node.collision=false; // Visual-only; do not spawn 50 box colliders.
            object.mass=0.f;
            object.size=1.f; // Preserve the imported model's original scale.
            model.objects.push_back(std::move(object));
        }
        validate_scene(model);
        auto const output=assetRoot.parent_path()/"prefabs"/
                          (folder.filename().u8string()+".nprefab");
        if(fs::exists(output)) {
            auto choice=QMessageBox::question(this,"Replace Model Prefab",
                "The prefab already exists. Replace it with the current source models?",
                QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
            if(choice!=QMessageBox::Yes)return;
        }
        fs::create_directories(output.parent_path());
        save_scene(model,output); // Current NEF8 binary Scene V3 codec.
        QMessageBox::information(this,"Native 3D Model Imported",
            QString("Created %1 with %2 renderable mesh parts (%3 excluded).\\n"
                    "The prefab is available in the editor and through Prefab.instantiate().")
                .arg(qt(output)).arg(parts.size()).arg(ignored));
        refresh();
    }catch(std::exception const& e) {
        QMessageBox::warning(this,"Model Prefab Import",QString::fromUtf8(e.what()));
    }
}
void QtContentBrowser::renameSelected() {
    renameMoveSelected();
}
void QtContentBrowser::duplicateSelected() {
    // Engine shared content is a read-only authoring mount. Use a
    // project override instead of mutating the engine installation.
    if(engine_selected_)return;
    if(selectedIsDirectory())return;
    if(auto const* rec=activeCatalog().find(selectedPath().toUtf8().toStdString());
       rec && rec->is_virtual_entry())return;
    auto path=selectedPath();if(path.isEmpty())return;
    try {
        auto old=activeCatalog().resolve(path.toUtf8().toStdString());
        auto dest=old.parent_path()/
            (old.stem().u8string()+"_copy"+old.extension().u8string());
        if(fs::exists(dest))throw std::runtime_error("Duplicate already exists");
        fs::copy_file(old,dest);
        refresh();
    } catch(std::exception const& e) {
        QMessageBox::warning(this,"Duplicate Asset",QString::fromUtf8(e.what()));
    }
}
void QtContentBrowser::removeSelected() {
    // Engine shared content is a read-only authoring mount. Use a
    // project override instead of mutating the engine installation.
    if(engine_selected_)return;
    auto path=selectedPath();
    if(auto const* rec=activeCatalog().find(path.toUtf8().toStdString());
       rec && rec->is_virtual_entry()) {
        QMessageBox::information(this,"NEF8 ListFile",
            "Embedded dictionary entries cannot be modified as physical files.");
        return;
    }if(path.isEmpty())return;
    if(QMessageBox::question(this,"Delete Asset",
        "Delete "+path+"? Folders must be empty; this cannot be undone.",
        QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    try {
        auto full=activeCatalog().resolve(path.toUtf8().toStdString());
        // Never recursively delete a directory tree, especially not with symlinks.
        if(!fs::remove(full))throw std::runtime_error("File/folder could not be removed");
        refresh();
    } catch(std::exception const& e) {
        QMessageBox::warning(this,"Delete Asset",QString::fromUtf8(e.what()));
    }
}
void QtContentBrowser::copyReference() {
    auto ref=selectedAsset();
    if(!ref.isEmpty())QApplication::clipboard()->setText(ref);
}
void QtContentBrowser::revealSelected() {
    auto ref=selectedPath();
    if(ref.isEmpty())return;
    try {
        auto full=activeCatalog().backing_file(ref.toUtf8().toStdString());
        auto parent=fs::is_directory(full)?full:full.parent_path();
        QDesktopServices::openUrl(QUrl::fromLocalFile(qt(parent)));
    } catch(std::exception const&) {}
}
void QtContentBrowser::toggleFavoriteSelected() {
    QString const folder=selectedIsDirectory()?selectedPath():folder_;
    if(root_.isEmpty() || folder.isEmpty())return;
    auto const* record=activeCatalog().find(folder.toUtf8().toStdString());
    if(!record || !record->directory)return;
    auto favorites=browser_favorites::load(root_);
    if(favorites.contains(folder))favorites.removeAll(folder);
    else if(favorites.size()<256)favorites.append(folder);
    browser_favorites::save(root_,favorites);
    populateTree();
}

void QtContentBrowser::showMenu(QPoint point) {
    QMenu menu(this);
    auto selected=selectedPath();
    auto* open=menu.addAction("Preview / Browse");
    auto* insert=menu.addAction("Add to Scene");
    auto* copy=menu.addAction("Copy Asset Reference");
    auto selection=selectedPaths();
    auto* copyAll=menu.addAction(
        QString("Copy %1 Asset References").arg(selection.size()));
    auto* duplicateBatch=menu.addAction(
        QString("Duplicate %1 Selected Assets...").arg(selection.size()));
    auto* deleteBatch=menu.addAction(
        QString("Move %1 Selected Assets to Recycle Bin...").arg(selection.size()));
    auto* collectionsMenu=menu.addMenu("Collections");
    collectionsMenu->addAction("New Collection...",this,&QtContentBrowser::createCollection);
    auto* teamMenu=collectionsMenu->addMenu("Team Collections (project)");
    teamMenu->setEnabled(!engine_selected_ && !project_root_.isEmpty());
    teamMenu->addAction("New Team Collection...",this,
                        &QtContentBrowser::createTeamCollection);
    teamMenu->addAction("Publish Local Collection...",this,
                        &QtContentBrowser::publishLocalCollection);
    auto* teamAdd=teamMenu->addMenu("Add Selected to Team");
    auto* addTo=collectionsMenu->addMenu("Add Selected to");
    auto* removeFrom=collectionsMenu->addMenu("Remove Selected from");
    for(auto const& name:asset_collections::load(root_).keys()) {
        addTo->addAction(name,this,[this,name]{addSelectionToCollection(name);});
        removeFrom->addAction(name,this,[this,name]{removeSelectionFromCollection(name);});
    }
    if(!engine_selected_) {
        asset_collections::Items team;
        QString issue;
        if(asset_team_collections::load(project_root_,team,issue)) {
            for(QString const& name:team.keys()) {
                QString id="@team/"+name;
                teamAdd->addAction(name,this,[this,id] {
                    addSelectionToCollection(id);
                });
                removeFrom->addAction("Team: "+name,this,[this,id] {
                    removeSelectionFromCollection(id);
                });
            }
        }
    }
    addTo->setEnabled(!selection.isEmpty() && !addTo->actions().isEmpty());
    removeFrom->setEnabled(!selection.isEmpty() && !removeFrom->actions().isEmpty());
    auto* dependencies=menu.addAction("Show Dependency Graph...");
    dependencies->setEnabled(!engine_selected_ &&
                             registry_ && !selected.isEmpty());
    auto* gitMenu=menu.addMenu("Source Control (Git)");
    gitMenu->addAction("Status...",this,&QtContentBrowser::showGitStatus);
    gitMenu->addAction("Diff...",this,&QtContentBrowser::showGitDiff);
    gitMenu->addSeparator();
    gitMenu->addAction("Stage Selected...",this,[this]{gitStage(true);});
    gitMenu->addAction("Unstage Selected...",this,[this]{gitStage(false);});
    menu.addSeparator();
    auto* create=menu.addAction("New Folder...");
    auto* import=menu.addAction("Import Files...");
    auto* nativePrefab=menu.addAction("Create Model Prefab from Folder...");
    auto* rename=menu.addAction("Rename...");
    auto* refactor=menu.addAction(
        QString("Rename / Move %1 Asset(s) with References...").arg(selection.size()));
    auto* duplicate=menu.addAction("Duplicate");
    auto* reveal=menu.addAction("Show Containing Folder");
    auto const favoriteFolder=selectedIsDirectory()?selected:folder_;
    auto const currentFavorites=browser_favorites::load(root_);
    auto* favorite=menu.addAction(currentFavorites.contains(favoriteFolder)?
        "Remove Folder from Favorites":"Add Folder to Favorites");
    auto const* targetFolder=activeCatalog().find(
        favoriteFolder.toUtf8().toStdString());
    favorite->setEnabled(targetFolder && targetFolder->directory);
    menu.addSeparator();
    auto* remove=menu.addAction("Delete...");
    open->setEnabled(!selected.isEmpty());
    auto const* chosen=activeCatalog().find(selected.toUtf8().toStdString());
    insert->setEnabled(chosen && !chosen->directory &&
        (chosen->kind==AssetKind::mesh || chosen->kind==AssetKind::prefab));
    if(chosen && chosen->kind==AssetKind::prefab)
        insert->setText("Instantiate Prefab in Scene");
    copy->setEnabled(!selected.isEmpty()&&!selectedIsDirectory());
    copyAll->setEnabled(!selection.isEmpty());
    bool editableGroup=!engine_selected_ && !selection.isEmpty() &&
        selection.size()<=256;
    for(auto const& path:selection) {
        auto const* record=activeCatalog().find(path.toUtf8().toStdString());
        if(!record || record->directory || record->is_list_file() ||
           record->is_virtual_entry() || record->external_prefab)
            editableGroup=false;
    }
    duplicateBatch->setEnabled(editableGroup && selection.size()>1);
    refactor->setEnabled(editableGroup && selection.size()<=128);
    deleteBatch->setEnabled(editableGroup && selection.size()>1);
    gitMenu->setEnabled(editableGroup);
    create->setEnabled(!engine_selected_ && !root_.isEmpty());
    import->setEnabled(!engine_selected_ && !root_.isEmpty());
    nativePrefab->setEnabled(!engine_selected_ && chosen &&
        chosen->directory && !chosen->is_list_file());
    rename->setEnabled(!engine_selected_ && !selected.isEmpty() &&
                       selected!="prefabs");
    duplicate->setEnabled(!engine_selected_ && !selected.isEmpty() &&
                           !selectedIsDirectory());
    reveal->setEnabled(!selected.isEmpty() && !(chosen && chosen->embedded_engine_api));
    remove->setEnabled(!engine_selected_ && !selected.isEmpty() &&
                       selected!="prefabs");
    auto* selectedAction=menu.exec(point.isNull()?QCursor::pos():
                                           grid_->viewport()->mapToGlobal(point));
    if(selectedAction==open) {
        if(selectedIsDirectory())openFolder(selected);
        else if(!selected.isEmpty())openAssetPreview(selected);
    } else if(selectedAction==insert) {
        if(chosen && (chosen->kind==AssetKind::mesh ||
                      chosen->kind==AssetKind::prefab))
            emit assetActivated(QString::fromUtf8(chosen->asset_reference().c_str()));
    } else if(selectedAction==copy)copyReference();
    else if(selectedAction==copyAll)copySelectedReferences();
    else if(selectedAction==duplicateBatch)duplicateSelectedBatch();
    else if(selectedAction==deleteBatch)deleteSelectedBatch();
    else if(selectedAction==create)createFolder();
    else if(selectedAction==import)importFiles();
    else if(selectedAction==nativePrefab)createNativeModelPrefab();
    else if(selectedAction==rename)renameSelected();
    else if(selectedAction==refactor)renameMoveSelected();
    else if(selectedAction==dependencies)showDependencyGraph();
    else if(selectedAction==duplicate)duplicateSelected();
    else if(selectedAction==reveal)revealSelected();
    else if(selectedAction==favorite)toggleFavoriteSelected();
    else if(selectedAction==remove)removeSelected();
}
} // namespace northstarcpp::editor
