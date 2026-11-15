// Isolated Content Browser file operations and context menus.
// Mutations remain project-only; read-only engine/NEF8 references are guarded.
#include "qt_content_browser.hpp"
#include "qt_content_browser_internal.hpp"
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
void QtContentBrowser::renameSelected() {
    // Engine shared content is a read-only authoring mount. Use a
    // project override instead of mutating the engine installation.
    if(engine_selected_)return;
    auto path=selectedPath();
    if(auto const* rec=activeCatalog().find(path.toUtf8().toStdString());
       rec && rec->is_virtual_entry()) {
        QMessageBox::information(this,"NEF8 ListFile",
            "Embedded dictionary entries cannot be modified as physical files.");
        return;
    }
    if(path.isEmpty())return;
    bool accepted=false;
    auto name=QInputDialog::getText(this,"Rename Asset","New name:",
        QLineEdit::Normal,path.section('/',-1),&accepted).trimmed();
    if(!accepted)return;
    if(!isSafeBasename(name)) {
        QMessageBox::warning(this,"Invalid Name","Use a safe asset name.");return;
    }
    try {
        auto old=activeCatalog().resolve(path.toUtf8().toStdString());
        auto dest=old.parent_path()/native(name);
        if(fs::exists(dest))
            throw std::runtime_error("An asset already exists with this name");
        fs::rename(old,dest);
        refresh();
    } catch(std::exception const& e) {
        QMessageBox::warning(this,"Rename Asset",QString::fromUtf8(e.what()));
    }
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
void QtContentBrowser::showMenu(QPoint point) {
    QMenu menu(this);
    auto selected=selectedPath();
    auto* open=menu.addAction("Preview / Browse");
    auto* insert=menu.addAction("Add to Scene");
    auto* copy=menu.addAction("Copy Asset Reference");
    menu.addSeparator();
    auto* create=menu.addAction("New Folder...");
    auto* import=menu.addAction("Import Files...");
    auto* rename=menu.addAction("Rename...");
    auto* duplicate=menu.addAction("Duplicate");
    auto* reveal=menu.addAction("Show Containing Folder");
    menu.addSeparator();
    auto* remove=menu.addAction("Delete...");
    open->setEnabled(!selected.isEmpty());
    auto const* chosen=activeCatalog().find(selected.toUtf8().toStdString());
    insert->setEnabled(chosen && !chosen->directory &&
        (chosen->kind==AssetKind::mesh || chosen->kind==AssetKind::prefab));
    if(chosen && chosen->kind==AssetKind::prefab)
        insert->setText("Instantiate Prefab in Scene");
    copy->setEnabled(!selected.isEmpty()&&!selectedIsDirectory());
    create->setEnabled(!engine_selected_ && !root_.isEmpty());
    import->setEnabled(!engine_selected_ && !root_.isEmpty());
    rename->setEnabled(!engine_selected_ && !selected.isEmpty() &&
                       selected!="prefabs");
    duplicate->setEnabled(!engine_selected_ && !selected.isEmpty() &&
                           !selectedIsDirectory());
    reveal->setEnabled(!selected.isEmpty());
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
    else if(selectedAction==create)createFolder();
    else if(selectedAction==import)importFiles();
    else if(selectedAction==rename)renameSelected();
    else if(selectedAction==duplicate)duplicateSelected();
    else if(selectedAction==reveal)revealSelected();
    else if(selectedAction==remove)removeSelected();
}
} // namespace northstarcpp::editor
