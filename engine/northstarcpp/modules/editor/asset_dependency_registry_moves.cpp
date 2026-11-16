#include "asset_dependency_registry.hpp"
#include <northstarcpp/scene.hpp>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <stdexcept>
#include <vector>
#include <unordered_map>
namespace northstarcpp::editor {
namespace fs=std::filesystem;
namespace {
struct Rewrite {
    fs::path path;
    QByteArray original,replacement;
    bool committed=false;
};
void writeAtomically(fs::path const& path,QByteArray const& data) {
    QSaveFile output(QString::fromStdWString(path.wstring()));
    if(!output.open(QIODevice::WriteOnly) ||
       output.write(data)!=data.size() || !output.commit())
        throw std::runtime_error("Atomic dependency update failed");
}
QByteArray readBounded(fs::path const& path) {
    QFile file(QString::fromStdWString(path.wstring()));
    if(!file.open(QIODevice::ReadOnly) || file.size()>16*1024*1024)
        throw std::runtime_error("Unreadable or oversized dependency source");
    QByteArray bytes=file.readAll();
    if(bytes.size()!=file.size())
        throw std::runtime_error("Truncated dependency source");
    return bytes;
}
bool inside(fs::path const& candidate,fs::path const& root) {
    auto a=candidate.begin();
    for(auto b=root.begin();b!=root.end();++b,++a)
        if(a==candidate.end() || *a!=*b)return false;
    return true;
}
QByteArray rewriteText(QByteArray const& bytes,
                      QHash<QString,QString> const& changes,int& count) {
    QString text=QString::fromUtf8(bytes);
    if(text.toUtf8()!=bytes)
        throw std::runtime_error("Dependency source has unsupported encoding");
    static QRegularExpression const token(
        R"re((["'])([^"'\r\n]{1,255})\1)re");
    QString output;output.reserve(text.size()+256);
    qsizetype start=0;
    auto iter=token.globalMatch(text);
    while(iter.hasNext()) {
        auto matched=iter.next();
        auto value=matched.captured(2);
        auto changed=changes.constFind(value);
        if(changed==changes.cend())continue;
        output+=text.mid(start,matched.capturedStart(2)-start);
        output+=*changed;
        start=matched.capturedEnd(2);
        ++count;
    }
    if(!count)return bytes;
    output+=text.mid(start);
    return output.toUtf8();
}
QByteArray rewriteScene(fs::path const& path,
                        QHash<QString,QString> const& changes,int& count) {
    auto document=northstarcpp::load_scene(path);
    for(auto& object:document.objects) {
        auto& asset=object.node.asset;
        auto& script=object.node.script;
        auto change=[&](std::string& value) {
            auto found=changes.constFind(QString::fromUtf8(value.c_str()));
            if(found==changes.cend())return;
            value=found->toUtf8().toStdString();
            ++count;
        };
        change(asset);change(script);
    }
    if(!count)return readBounded(path);
    northstarcpp::validate_scene(document);
    std::string packed=northstarcpp::serialize_scene_binary(document);
    return QByteArray(packed.data(),static_cast<qsizetype>(packed.size()));
}
}
AssetDependencyRegistry::Result AssetDependencyRegistry::moveAssets(
    AssetCatalog const& catalog,QVector<Move> const& moves) const {
    Result result;
    if(moves.empty() || moves.size()>128) {
        result.message="Choose 1-128 physical project assets";return result;
    }
    if(root_.isEmpty() ||
       QString::fromStdWString(catalog.root().wstring())!=root_) {
        result.message="Asset Registry snapshot is stale";return result;
    }
    std::vector<std::pair<fs::path,fs::path>> movePaths;
    QHash<QString,QString> changes;
    try {
        fs::path root=fs::weakly_canonical(catalog.root());
        QSet<QString> destinations,sources;
        for(auto const& item:moves) {
            if(!safe_virtual_path(item.from.toUtf8().toStdString()) ||
               !safe_virtual_path(item.to.toUtf8().toStdString()) ||
               item.from==item.to || destinations.contains(item.to) ||
               sources.contains(item.from))
                throw std::invalid_argument("Invalid, duplicate or unchanged destination");
            auto const* original=catalog.find(item.from.toUtf8().toStdString());
            if(!original || original->directory || original->is_list_file() ||
               original->is_virtual_entry() || original->external_prefab)
                throw std::invalid_argument("Only physical project assets can move");
            fs::path from=catalog.resolve(original->virtual_path);
            fs::path to=catalog.root()/fs::u8path(item.to.toUtf8().toStdString());
            std::error_code ec;
            if(fs::exists(to,ec) || fs::is_symlink(to,ec) ||
               !fs::is_regular_file(from,ec) || fs::is_symlink(from,ec))
                throw std::invalid_argument("Destination exists or source is not regular");
            fs::path parent=fs::weakly_canonical(to.parent_path());
            if(!inside(parent,root) || !fs::is_directory(parent))
                throw std::invalid_argument("Destination must be inside an existing project folder");
            if(!inside(fs::weakly_canonical(from),root))
                throw std::invalid_argument("Asset source escapes its project root");
            destinations.insert(item.to);sources.insert(item.from);
            changes.insert(item.from,item.to);
            movePaths.emplace_back(std::move(from),std::move(to));
        }
        std::vector<Rewrite> updates;
        std::size_t originalBytes=0;
        int changedRefs=0;
        for(auto const& source:sources_) {
            bool relevant=false;
            for(QString const& ref:source.dependencies)
                if(changes.contains(ref)) {relevant=true;break;}
            if(!relevant)continue;
            auto const bytes=readBounded(source.path);
            originalBytes+=static_cast<std::size_t>(bytes.size());
            if(originalBytes>64u*1024u*1024u)
                throw std::length_error("Dependency backups exceed 64 MiB safety limit");
            int count=0;
            QByteArray output=source.format==Format::scene?
                rewriteScene(source.path,changes,count):
                rewriteText(bytes,changes,count);
            if(!count)
                throw std::runtime_error("Indexed dependency changed since snapshot");
            updates.push_back({source.path,bytes,std::move(output),false});
            changedRefs+=count;
        }
        // All writes are prepared before the first rename. Once prepared,
        // rollback restores the original asset names and original file bytes.
        std::size_t renamed=0;
        try {
            for(auto const& pair:movePaths) {
                fs::rename(pair.first,pair.second);
                ++renamed;
            }
            for(auto& update:updates) {
                for(auto const& pair:movePaths)
                    if(pair.first==update.path) {
                        update.path=pair.second;
                        break;
                    }
                writeAtomically(update.path,update.replacement);
                update.committed=true;
            }
        }catch(...) {
            bool rolledBack=true;
            for(auto const& update:updates) {
                if(!update.committed)continue;
                try {writeAtomically(update.path,update.original);}
                catch(...) {rolledBack=false;}
            }
            while(renamed) {
                --renamed;
                try {fs::rename(movePaths[renamed].second,
                                movePaths[renamed].first);}
                catch(...) {rolledBack=false;}
            }
            throw std::runtime_error(rolledBack?
                "Transaction failed; original references and paths restored":
                "Transaction failed; rollback incomplete: inspect changed files");
        }
        result.ok=true;
        result.assetsMoved=static_cast<int>(movePaths.size());
        result.referencesUpdated=changedRefs;
        result.message=QString("Moved %1 assets; updated %2 references")
            .arg(result.assetsMoved).arg(changedRefs);
    }catch(std::exception const& e) {
        result.message=QString::fromUtf8(e.what());
    }
    return result;
}
}
