#include "qt_content_browser.hpp"
#include "qt_content_browser_internal.hpp"
#include "qt_asset_card_delegate.hpp"
#include "qt_content_import.hpp"

#include <QtConcurrent/QtConcurrentRun>
#include <QCheckBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QMimeData>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <algorithm>

namespace northstarcpp::editor {
void QtContentBrowser::importFiles() {
    if(engine_selected_ || import_busy_)return;
    if(content_import::destination(project_root_,folder_).isEmpty()) {
        QMessageBox::information(this,"Import Assets","Choose a project folder or an editable YTD, YDD or NAC dictionary.");
        return;
    }
    QFileDialog dialog(this,"Select Source Assets");
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setOption(QFileDialog::DontUseNativeDialog,true);
    dialog.setNameFilter(content_import::fileFilter());
    if(dialog.exec()==QDialog::Accepted)
        importSources(dialog.selectedFiles(),folder_);
}

bool QtContentBrowser::eventFilter(QObject* watched,QEvent* event) {
    bool const tree=watched==folders_->viewport();
    if(!tree && watched!=grid_->viewport())
        return QWidget::eventFilter(watched,event);
    if(event->type()==QEvent::DragLeave) {
        QToolTip::hideText();
        return QWidget::eventFilter(watched,event);
    }
    if(event->type()!=QEvent::DragEnter && event->type()!=QEvent::DragMove &&
       event->type()!=QEvent::Drop)
        return QWidget::eventFilter(watched,event);

    auto* drop=static_cast<QDropEvent*>(event);
    auto const* mime=drop->mimeData();
    if(!mime || !mime->hasUrls() || mime->hasFormat("application/x-northstar-asset-reference"))
        return QWidget::eventFilter(watched,event);

    auto const position=drop->position().toPoint();
    auto const sources=content_import::localFiles(*mime);
    QString folder=folder_;
    bool project=!engine_selected_;
    if(tree) {
        auto* item=folders_->itemAt(position);
        project=item && item->data(0,browser_internal::folderSourceRole).toInt()==0;
        if(project)folder=item->data(0,asset_card_roles::path).toString();
    } else if(project) {
        auto const item=grid_->indexAt(position);
        auto const path=itemPath(item);
        if(auto const* record=activeCatalog().find(path.toUtf8().toStdString());
           record && record->directory)
            folder=path;
    }

    auto const target=project?content_import::destination(project_root_,folder):QString{};
    bool const dictionary=QFileInfo(target).isFile();
    auto const count=std::count_if(sources.cbegin(),sources.cend(),
        [&](QString const& source) {
            return content_import::compatible(source,dictionary?target:QString{});
        });
    bool const acceptable=!target.isEmpty() && !import_busy_ && count>0 &&
        drop->possibleActions().testFlag(Qt::CopyAction);
    if(acceptable) {
        drop->setDropAction(Qt::CopyAction);
        drop->accept();
    } else {
        drop->ignore();
    }
    if(event->type()==QEvent::Drop) {
        QToolTip::hideText();
        if(acceptable)importSources(sources,folder);
    } else {
        auto const text=acceptable
            ? QString(dictionary?"Add %1 file(s) to %2":"Import %1 file(s) into Project / %2")
                .arg(count).arg(folder)
            : import_busy_?QString("An import is already running.")
            : target.isEmpty()?QString("Drop onto a project folder or a YTD, YDD or NAC dictionary.")
            : QString("No compatible local files for this destination.");
        QToolTip::showText(QCursor::pos()+QPoint(12,15),text,
                           static_cast<QWidget*>(watched),{},1600);
    }
    return true;
}

void QtContentBrowser::importSources(QStringList const& sources,QString const& folder) {
    if(sources.isEmpty() || import_busy_)return;
    auto const project=project_root_;
    auto const target=content_import::destination(project,folder);
    if(target.isEmpty())return;

    bool const dictionary=QFileInfo(target).isFile();
    QDialog dialog(this);
    dialog.setWindowTitle(dictionary?"Add to Dictionary":"Import Project Assets");
    dialog.setObjectName("nativeImportDialog");
    QFormLayout form(&dialog);
    auto* description=new QLabel(
        QString("%1 source file(s) → Project / %2").arg(sources.size()).arg(folder),&dialog);
    description->setTextFormat(Qt::PlainText);
    description->setWordWrap(true);
    form.addRow(description);
    auto* formats=new QLabel("Textures → YTD   Models → YDD/YDR   Audio → NAC", &dialog);
    form.addRow(formats);
    auto* group=new QLineEdit(&dialog);
    group->setObjectName("nativeImportGroupName");
    group->setPlaceholderText("Leave blank for one asset per source");
    group->setMaxLength(100);
    if(!dictionary)form.addRow("Group name",group);
    else group->hide();
    auto* singleDrawable=new QCheckBox("Use YDR for single-model assets",&dialog);
    singleDrawable->setObjectName("nativeImportSingleDrawable");
    if(!dictionary)form.addRow(singleDrawable);
    else singleDrawable->hide();
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(dictionary?"Add":"Import");
    form.addRow(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted || project!=project_root_ ||
       target!=content_import::destination(project,folder))return;

    content_import::Options const options{
        dictionary?QFileInfo(target).absolutePath():target,group->text().trimmed(),
        singleDrawable->isChecked(),dictionary?target:QString{}};
    import_busy_=true;
    import_->setText("Importing…");
    import_->setEnabled(false);
    status_->setText(QString("Importing %1 source file(s)…").arg(sources.size()));
    auto* watcher=new QFutureWatcher<content_import::Result>(this);
    connect(watcher,&QFutureWatcher<content_import::Result>::finished,this,
            [this,watcher,project] {
        auto const result=watcher->result();
        watcher->deleteLater();
        import_busy_=false;
        import_->setText("Import");
        import_->setEnabled(!engine_selected_ && QFileInfo(project_root_).isDir());
        if(project==project_root_ && (!result.created.isEmpty() || !result.updated.isEmpty()))
            refresh();
        auto const summary=QString("Created %1 · Updated %2 · Failed %3")
            .arg(result.created.size()).arg(result.updated.size()).arg(result.errors.size());
        QMessageBox report(result.errors.isEmpty()?QMessageBox::Information:QMessageBox::Warning,
                           "Asset Import",summary,QMessageBox::Ok,this);
        report.setTextFormat(Qt::PlainText);
        QString details;
        if(!result.created.isEmpty())details="Created\n"+result.created.join('\n');
        if(!result.updated.isEmpty())details="Updated\n"+result.updated.join('\n');
        if(!result.errors.isEmpty()) {
            if(!details.isEmpty())details+="\n\n";
            details+="Errors\n"+result.errors.join('\n');
        }
        report.setDetailedText(details);
        if(!result.errors.isEmpty())report.setInformativeText(result.errors.front());
        report.exec();
    });
    watcher->setFuture(QtConcurrent::run([sources,options] {
        return content_import::run(sources,options);
    }));
}
} // namespace northstarcpp::editor
