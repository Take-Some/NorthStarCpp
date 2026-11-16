#include "asset_dependency_registry.hpp"
#include <northstarcpp/scene.hpp>
#include <QFile>
#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace northstarcpp::editor {
namespace fs=std::filesystem;
namespace {
bool candidate(QString const& name) {
    const auto ext=QFileInfo(name).suffix().toLower();
    return QStringList{"xml","ymat","json","js","mjs","ts","nscn","nscene","nprefab"}
        .contains(ext);
}
QStringList quotedReferences(QByteArray const& bytes,QSet<QString> const& known) {
    QStringList deps;
    // Only exact quoted literals matching indexed VFS references are dependencies.
    static const QRegularExpression token(
        R"re((["'])([^"'\r\n]{1,255})\1)re");
    auto iter=token.globalMatch(QString::fromUtf8(bytes));
    while(iter.hasNext()) {
        auto value=iter.next().captured(2);
        if(known.contains(value) && !deps.contains(value))
            deps.append(value);
    }
    return deps;
}
void add(QStringList& dest,QString const& text,QSet<QString> const& known) {
    if(known.contains(text) && !dest.contains(text))dest.append(text);
}
}
bool AssetDependencyRegistry::rebuild(AssetCatalog const& catalog,QString& error) {
    QVector<Source> updated;
    QHash<QString,QStringList> inbound,outbound;
    QSet<QString> known;
    for(auto const& rec:catalog.entries()) {
        known.insert(QString::fromUtf8(rec.virtual_path.c_str()));
        known.insert(QString::fromUtf8(rec.asset_reference().c_str()));
    }
    fs::path root=catalog.root();
    QString const rootText=QString::fromStdWString(root.wstring());
    int skipped=0;
    auto scan=[&](QString const& id,fs::path const& path,
                  std::uintmax_t size) {
        if(updated.size()>=4096 || size>8u*1024u*1024u ||
           !candidate(QString::fromStdWString(path.filename().wstring()))) {
            ++skipped;return;
        }
        std::error_code ec;
        if(!fs::is_regular_file(path,ec) || fs::is_symlink(path,ec))return;
        Source source;source.name=id;source.path=path;
        auto ext=path.extension().u8string();
        if(ext==".nscn" || ext==".nscene" || ext==".nprefab") {
            try {
                auto document=northstarcpp::load_scene(path);
                source.format=Format::scene;
                for(auto const& object:document.objects) {
                    add(source.dependencies,QString::fromUtf8(object.node.asset.c_str()),known);
                    add(source.dependencies,QString::fromUtf8(object.node.script.c_str()),known);
                }
            } catch(std::exception const&) {++skipped;return;}
        } else {
            QFile file(QString::fromStdWString(path.wstring()));
            if(!file.open(QIODevice::ReadOnly)) {++skipped;return;}
            auto bytes=file.read(8u*1024u*1024u+1);
            if(bytes.size()>8*1024*1024 || bytes.startsWith("NEF8") ||
               bytes.contains('\0')) {++skipped;return;}
            source.dependencies=quotedReferences(bytes,known);
        }
        outbound.insert(id,source.dependencies);
        for(auto const& ref:source.dependencies)inbound[ref].append(id);
        updated.append(std::move(source));
    };
    for(auto const& rec:catalog.entries()) {
        if(rec.directory || rec.is_virtual_entry())continue;
        // Source files outside the physical project are not enumerated.
        if(!candidate(QString::fromUtf8(rec.filename.c_str())))continue;
        try {
            auto path=catalog.backing_file(rec.virtual_path);
            scan(QString::fromUtf8(rec.virtual_path.c_str()),path,rec.size);
        }catch(std::exception const&) {++skipped;}
    }
    // Scene documents are siblings of assets/, not necessarily indexed
    // by Content Browser. Include them as project-owned reference sources.
    auto parent=root.parent_path();
    if(root.filename()=="assets" && fs::is_directory(parent)) {
        for(auto const& entry:fs::directory_iterator(parent)) {
            std::error_code ec;
            if(entry.is_symlink(ec) || !entry.is_regular_file(ec))continue;
            auto extension=entry.path().extension().u8string();
            if(extension!=".nscn" && extension!=".nscene")continue;
            scan("@project/"+QString::fromStdWString(
                     entry.path().filename().wstring()),
                 entry.path(),entry.file_size(ec));
        }
    }
    root_=rootText;
    sources_=std::move(updated);
    inbound_=std::move(inbound);
    outbound_=std::move(outbound);
    edge_count_=0;
    for(auto it=outbound_.cbegin();it!=outbound_.cend();++it)
        edge_count_+=it.value().size();
    ++revision_;
    error=skipped?QString("%1 unsupported, invalid or oversized sources skipped")
                    .arg(skipped):QString{};
    return true;
}
QStringList AssetDependencyRegistry::dependenciesOf(QString const& id) const {
    return outbound_.value(id);
}
QStringList AssetDependencyRegistry::referencersOf(QString const& id) const {
    return inbound_.value(id);
}
QStringList AssetDependencyRegistry::watchedDirectories() const {
    QStringList dirs;
    for(auto const& s:sources_) {
        auto dir=QString::fromStdWString(s.path.parent_path().wstring());
        if(!dirs.contains(dir))dirs.append(dir);
    }
    if(!root_.isEmpty() && !dirs.contains(root_))dirs.append(root_);
    return dirs;
}
}
