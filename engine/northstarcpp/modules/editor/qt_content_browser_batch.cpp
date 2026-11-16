// Project-only multi-asset operations. Never mutate engine shared content,
// NEF8 virtual entries, external prefab mirrors, or symlink destinations.
#include "qt_content_browser.hpp"
#include "qt_content_browser_internal.hpp"
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QDir>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace northstarcpp::editor {
namespace fs=std::filesystem;
void QtContentBrowser::copySelectedReferences() {
    QStringList references;
    for(auto const& path:selectedPaths()) {
        auto const* record=activeCatalog().find(path.toUtf8().toStdString());
        if(!record || record->directory)continue;
        references.append(QString::fromUtf8(record->asset_reference().c_str()));
    }
    if(!references.isEmpty())
        QApplication::clipboard()->setText(references.join('\n'));
}
namespace {
std::vector<fs::path> editableFiles(AssetCatalog const& catalog,
                                   QStringList const& virtualPaths) {
    if(virtualPaths.size()>256)
        throw std::invalid_argument("Batch operation limited to 256 assets");
    std::vector<fs::path> files;
    for(QString const& selected:virtualPaths) {
        auto* item=catalog.find(selected.toUtf8().toStdString());
        if(!item || item->directory || item->is_list_file() ||
           item->is_virtual_entry() || item->external_prefab)
            throw std::invalid_argument(
                "Batch operation requires physical, editable project files");
        fs::path file=catalog.resolve(item->virtual_path);
        std::error_code error;
        if(fs::is_symlink(file,error) || !fs::is_regular_file(file,error))
            throw std::invalid_argument("Refusing non-regular file or symbolic link");
        files.push_back(std::move(file));
    }
    return files;
}
}
void QtContentBrowser::duplicateSelectedBatch() {
    if(engine_selected_ || root_.isEmpty())return;
    try {
        auto files=editableFiles(activeCatalog(),selectedPaths());
        if(files.empty())return;
        std::vector<fs::path> destinations;
        destinations.reserve(files.size());
        for(auto const& file:files) {
            fs::path target=file.parent_path()/
                (file.stem().u8string()+"_copy"+file.extension().u8string());
            if(fs::exists(target))
                throw std::runtime_error("Duplicate target already exists");
            destinations.push_back(std::move(target));
        }
        // Preflight all targets and report conflicts before copying anything.
        if(QMessageBox::question(this,"Duplicate Assets",
            QString("Duplicate %1 selected file(s)?").arg(files.size()),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)
            return;
        std::vector<fs::path> created;
        try {
            for(std::size_t i=0;i<files.size();++i) {
                fs::copy_file(files[i],destinations[i],fs::copy_options::none);
                created.push_back(destinations[i]);
            }
        }catch(...) {
            for(auto const& path:created) {
                std::error_code ec;
                fs::remove(path,ec);
            }
            throw;
        }
        refresh();
    }catch(std::exception const& e) {
        QMessageBox::warning(this,"Batch Duplicate",QString::fromUtf8(e.what()));
    }
}
void QtContentBrowser::deleteSelectedBatch() {
    if(engine_selected_ || root_.isEmpty())return;
    try {
        auto files=editableFiles(activeCatalog(),selectedPaths());
        if(files.empty())return;
        if(QMessageBox::warning(this,"Move Assets to Recycle Bin",
            QString("Move %1 project asset(s) to the Recycle Bin?\n"
                    "References in scenes may need updating.").arg(files.size()),
            QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)
            return;
        QStringList failures;
        for(auto const& file:files)
            if(!QFile::moveToTrash(QString::fromStdWString(file.wstring())))
                failures.append(QString::fromStdWString(file.wstring()));
        refresh();
        if(!failures.isEmpty())
            QMessageBox::warning(this,"Batch Delete",
                QString("%1 file(s) could not be moved to the Recycle Bin.")
                    .arg(failures.size()));
    }catch(std::exception const& e) {
        QMessageBox::warning(this,"Batch Delete",QString::fromUtf8(e.what()));
    }
}
}
