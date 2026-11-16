#pragma once
// Git-shareable Collections live in the project, not in user QSettings.
// This file contains ONLY virtual asset references, never payloads.
#include "asset_browser_collections.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace northstarcpp::editor::asset_team_collections {
inline QString filename(QString const& assetsRoot) {
    if(QFileInfo(assetsRoot).fileName().compare("assets",Qt::CaseInsensitive)!=0)
        return {};
    return QFileInfo(assetsRoot).absoluteDir().filePath(
        ".northstar/content_collections.json");
}
inline bool load(QString const& assetsRoot,asset_collections::Items& result,
                 QString& error) {
    result.clear();error.clear();
    QString path=filename(assetsRoot);
    if(path.isEmpty()){error="Project Content root must be named assets";return false;}
    QFile file(path);
    if(!file.exists())return true;
    if(QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly)) {
        error="Cannot read team Collections or path is a symlink";return false;
    }
    if(file.size()>1024*1024){error="Team Collections file too large";return false;}
    QJsonParseError parse{};
    auto doc=QJsonDocument::fromJson(file.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError || !doc.isObject() ||
       doc.object().value("schema").toString()!="northstar.collections.v1") {
        error="Unsupported/corrupt team Collections document";return false;
    }
    auto data=doc.object().value("collections").toObject();
    if(data.size()>128){error="Too many team Collections";return false;}
    for(auto it=data.constBegin();it!=data.constEnd();++it) {
        if(!asset_collections::validName(it.key()) || !it.value().isArray()) {
            error="Invalid collection name or entries";return false;
        }
        QStringList refs;
        for(auto const& value:it.value().toArray()) {
            auto ref=value.toString();
            if(!value.isString() ||
               !safe_virtual_path(ref.toUtf8().toStdString()) ||
               refs.contains(ref) || refs.size()>=2048) {
                error="Invalid or duplicate asset reference";return false;
            }
            refs.append(ref);
        }
        result.insert(it.key(),refs);
    }
    return true;
}
inline bool save(QString const& assetsRoot,
                 asset_collections::Items const& collections,QString& error) {
    error.clear();
    QString const target=filename(assetsRoot);
    if(target.isEmpty()){error="Missing project assets root";return false;}
    if(collections.size()>128){error="Too many team Collections";return false;}
    QFileInfo existing(target);
    if(existing.exists() && existing.isSymLink()) {
        error="Team Collections target is a symlink";return false;
    }
    auto dir=existing.absoluteDir();
    if(!dir.exists() && !QDir().mkpath(dir.path())) {
        error="Could not create project metadata folder";return false;
    }
    if(QFileInfo(dir.path()).isSymLink()) {
        error="Project metadata folder is a symlink";return false;
    }
    QJsonObject data;
    for(auto it=collections.cbegin();it!=collections.cend();++it) {
        if(!asset_collections::validName(it.key()) ||
           it.value().size()>2048) {
            error="Invalid collection data";return false;
        }
        QJsonArray refs;
        for(QString const& path:it.value()) {
            if(!safe_virtual_path(path.toUtf8().toStdString())) {
                error="Unsafe collection reference";return false;
            }
            refs.append(path);
        }
        data.insert(it.key(),refs);
    }
    QJsonObject root;
    root.insert("schema","northstar.collections.v1");
    root.insert("collections",data);
    QSaveFile out(target);
    if(!out.open(QIODevice::WriteOnly)) {
        error="Could not open team Collections for save";return false;
    }
    auto bytes=QJsonDocument(root).toJson(QJsonDocument::Indented);
    if(out.write(bytes)!=bytes.size() || !out.commit()) {
        error="Atomic team Collections save failed";return false;
    }
    return true;
}
}
