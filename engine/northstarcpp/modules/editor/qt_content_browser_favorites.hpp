#pragma once
#include <QCryptographicHash>
#include <QSettings>
#include <QString>
#include <QStringList>
namespace northstarcpp::editor::browser_favorites {
inline QString key(QString const& root) {
    return QStringLiteral("ContentBrowser/Folders/")+
        QString::fromLatin1(QCryptographicHash::hash(
            root.toUtf8(),QCryptographicHash::Sha256).toHex());
}
inline QStringList load(QString const& root) {
    if(root.isEmpty())return {};
    QSettings state(QStringLiteral("Kayla's Systems"),
                    QStringLiteral("NorthStarCpp"));
    return state.value(key(root)).toStringList();
}
inline void save(QString const& root,QStringList const& values) {
    if(root.isEmpty())return;
    QSettings state(QStringLiteral("Kayla's Systems"),
                    QStringLiteral("NorthStarCpp"));
    state.setValue(key(root),values);
    state.sync();
}
}
