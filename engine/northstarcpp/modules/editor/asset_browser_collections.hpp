#pragma once
// Named, non-owning collections. References are source-root scoped and stored
// in QSettings; no asset payload or project files are copied or rewritten.
#include <northstarcpp/editor_asset_catalog.hpp>
#include <QCryptographicHash>
#include <QSettings>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace northstarcpp::editor::asset_collections {
using Items=QMap<QString,QStringList>;
inline QString storageKey(QString const& root) {
    auto hash=QCryptographicHash::hash(root.toUtf8(),QCryptographicHash::Sha256);
    return "ContentBrowser/Collections/v1/"+
        QString::fromLatin1(hash.toHex());
}
inline bool validName(QString const& name) {
    if(name.isEmpty() || name.size()>64 || name.trimmed()!=name)
        return false;
    for(QChar c:name)
        if(!c.isLetterOrNumber() && c!=' ' && c!='_' && c!='-')
            return false;
    return true;
}
inline Items load(QString const& root) {
    if(root.isEmpty())return {};
    QSettings s("Kayla's Systems","NorthStarCpp");
    auto values=s.value(storageKey(root)).toMap();
    Items result;
    for(auto it=values.cbegin();it!=values.cend();++it) {
        if(!validName(it.key()))continue;
        QStringList entries;
        for(auto const& item:it.value().toStringList()) {
            auto encoded=item.toUtf8().toStdString();
            if(safe_virtual_path(encoded) && !entries.contains(item) &&
               entries.size()<2048)
                entries.append(item);
        }
        result.insert(it.key(),entries);
        if(result.size()>=128)break;
    }
    return result;
}
inline void save(QString const& root,Items const& collections) {
    if(root.isEmpty())return;
    QVariantMap encoded;
    int count=0;
    for(auto it=collections.cbegin();it!=collections.cend() && count<128;++it) {
        if(!validName(it.key()))continue;
        QStringList refs;
        for(QString const& path:it.value()) {
            if(safe_virtual_path(path.toUtf8().toStdString()) &&
               !refs.contains(path) && refs.size()<2048)
                refs.append(path);
        }
        encoded.insert(it.key(),refs);
        ++count;
    }
    QSettings s("Kayla's Systems","NorthStarCpp");
    s.setValue(storageKey(root),encoded);
    s.sync();
}
inline bool add(Items& collections,QString const& name,QString const& reference) {
    if(!validName(name) || !collections.contains(name) ||
       !safe_virtual_path(reference.toUtf8().toStdString()))
        return false;
    auto& items=collections[name];
    if(items.contains(reference))return true;
    if(items.size()>=2048)return false;
    items.append(reference);
    return true;
}
}
