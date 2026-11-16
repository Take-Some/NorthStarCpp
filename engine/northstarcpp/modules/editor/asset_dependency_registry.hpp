#pragma once
// Dependency-aware authoring index. Never modifies a container member or
// engine Shared Assets. Scene references are parsed using current Scene ABI.
#include <northstarcpp/editor_asset_catalog.hpp>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVector>
#include <filesystem>

namespace northstarcpp::editor {
class AssetDependencyRegistry {
public:
    enum class Format { text, scene };
    struct Source {
        QString name; // virtual file ID or @project/main.nscn
        std::filesystem::path path;
        Format format=Format::text;
        QStringList dependencies;
    };
    struct Move {QString from;QString to;};
    struct Result {
        bool ok=false;
        QString message;
        int referencesUpdated=0;
        int assetsMoved=0;
    };

    bool rebuild(AssetCatalog const& catalog,QString& error);
    QStringList dependenciesOf(QString const& source) const;
    QStringList referencersOf(QString const& asset) const;
    int sourceCount() const noexcept {return sources_.size();}
    int edgeCount() const noexcept {return edge_count_;}
    int revision() const noexcept {return revision_;}
    QStringList watchedDirectories() const;
    QVector<Source> const& sources() const {return sources_;}
    Result moveAssets(AssetCatalog const& catalog,
                      QVector<Move> const& moves) const;
private:
    QVector<Source> sources_;
    QHash<QString,QStringList> outbound_,inbound_;
    QString root_;
    int edge_count_=0,revision_=0;
};
}
