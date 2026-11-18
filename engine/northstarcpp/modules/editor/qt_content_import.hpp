#pragma once

#include <QString>
#include <QStringList>

class QMimeData;

namespace northstarcpp::editor::content_import {
QStringList localFiles(QMimeData const& mime);
bool supported(QString const& file);
bool compatible(QString const& source,QString const& dictionary);
bool appendable(QString const& file);
QString destination(QString const& root, QString const& relativeFolder);
QString fileFilter();

struct Options {
    QString destination;
    QString groupName; // An empty name imports each source into its own asset.
    bool singleDrawable = false;
    QString dictionary;
};

struct Result {
    QStringList created;
    QStringList updated;
    QStringList errors;
};

Result run(QStringList const& sources, Options const& options);
} // namespace northstarcpp::editor::content_import
