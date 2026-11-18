// Native Markdown preview and UTF-8 authoring for NorthStar project scripts and shaders.
// This module does not execute JavaScript or compile shaders.
#include "qt_text_asset_editor.hpp"
#include <northstarcpp/embedded_script_api.hpp>
#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QTimer>
#include <QCompleter>
#include <QStandardItemModel>
#include <QAbstractItemView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QInputDialog>
#include <QMenu>
#include <QProcess>
#include <QListWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QShortcut>
#include <QTextCursor>
#include <QTextDocument>
#include <QDir>
#include <QSet>
#include <QPointer>
#include <functional>
#include <memory>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QKeySequence>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShortcut>
#include <QTextEdit>
#include <QTextFormat>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <stdexcept>

namespace northstarcpp::editor::text_assets {
namespace {
constexpr qint64 max_source_bytes=8*1024*1024;

QString formatTitle(Format format) {
    switch(format) {
    case Format::Markdown:return "Markdown Preview";
    case Format::JavaScript:return "JavaScript Editor";
    case Format::VertexShader:return "GLSL Vertex Shader Editor";
    case Format::FragmentShader:return "GLSL Fragment Shader Editor";
    case Format::TypeScript:return "TypeScript Editor";
    case Format::Json:return "JSON Editor";
    case Format::Plain:return "Text Editor";
    default:return "Text Asset";
    }
}

struct DocumentBytes {
    QString source;
    QByteArray original;
    bool bom=false;
    bool crlf=false;
};

DocumentBytes readFile(QString const& path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(("Cannot open: "+file.errorString()).toUtf8().toStdString());
    if(file.size()>max_source_bytes)
        throw std::runtime_error("Text asset exceeds the 8 MiB editor limit");
    QByteArray bytes=file.readAll();
    if(bytes.size()>max_source_bytes || bytes.contains('\0'))
        throw std::runtime_error("Binary or oversized text asset");
    bool const bom=bytes.startsWith("\xEF\xBB\xBF");
    QByteArray const utf8=bom?bytes.mid(3):bytes;
    QString const text=QString::fromUtf8(utf8.constData(),utf8.size());
    if(text.toUtf8()!=utf8)
        throw std::runtime_error("Invalid UTF-8: source left unchanged");
    return {text,bytes,bom,bytes.contains("\r\n")};
}

// The completion index is built from the engine's immutable, generated
// TypeScript declarations: no stale hard-coded lists of engine.* functions.
QStandardItemModel* apiCompletionModel(QObject* parent) {
    auto* model=new QStandardItemModel(parent);
    QSet<QString> seen;
    auto append=[&](QString const& symbol,QString const& signature) {
        if(symbol.isEmpty() || seen.contains(symbol))return;
        seen.insert(symbol);
        auto* item=new QStandardItem(symbol);
        item->setToolTip(signature);
        model->appendRow(item);
    };
    static QRegularExpression const ownerPattern(
        R"(^\s{4}export\s+(?:class|interface|type)\s+([A-Za-z_$][\w$]*))");
    static QRegularExpression const memberPattern(
        R"(^\s{8}(?:(?:static|readonly|public|private|protected)\s+)*([A-Za-z_$][\w$]*)\s*(?:\(|\??:))");
    auto const* entries=embedded_api::entries();
    for(std::size_t i=0;i<embedded_api::count();++i) {
        if(!QString::fromStdString(std::string(entries[i].virtual_path))
                .endsWith("northstar-engine.d.ts"))continue;
        QString owner,comment;
        QString const source=QString::fromUtf8(entries[i].source.data(),
                                                qsizetype(entries[i].source.size()));
        for(QString const& line:source.split('\n')) {
            auto cls=ownerPattern.match(line);
            if(cls.hasMatch()) {
                owner=cls.captured(1);
                append(owner,line.trimmed());
                comment.clear();
                continue;
            }
            if(line.startsWith("    }") || line.startsWith("    export type ")) {
                owner.clear();comment.clear();continue;
            }
            if(line.trimmed().startsWith("/**")) {
                comment=line.trimmed();
                continue;
            }
            if(owner.isEmpty())continue;
            auto member=memberPattern.match(line);
            if(member.hasMatch()) {
                auto const signature=line.trimmed()+
                    (comment.isEmpty()?QString{}:QString("\n")+comment);
                append(owner+"."+member.captured(1),signature);
                comment.clear();
            }
        }
    }
    model->sort(0,Qt::AscendingOrder);
    return model;
}

// One persistent TypeScript checker for all open JS/TS tabs. Never runs user
// source code: only the SDK declarations and current editor buffer are parsed.
// If Node/TypeScript is absent, QCompleter retains d.ts-indexed fallback.
class TypeScriptCompletionService final:public QObject {
public:
    using Callback=std::function<void(QJsonObject const&)>;
    static TypeScriptCompletionService& shared() {
        static auto* instance=new TypeScriptCompletionService(
            QCoreApplication::instance());
        return *instance;
    }
    bool complete(QString const& source,int position,QString const& language,
                  QString const& prefix,Callback callback) {
        return request("complete",source,position,language,
                       QJsonObject{{"prefix",prefix}},std::move(callback));
    }
    bool request(QString const& method,QString const& source,int position,
                 QString const& language,QJsonObject options,Callback callback) {
        if(!process_ || source.size()>800000 ||
           position<0 || position>source.size())return false;
        if(process_->state()==QProcess::Starting) {
            // The first editor request may arrive before Node initializes.
            // Queue it on startup; never block the GUI event thread.
            connect(process_,&QProcess::started,this,
                [this,method,source,position,language,options,
                 callback=std::move(callback)]() mutable {
                    request(method,source,position,language,options,
                            std::move(callback));
                },Qt::SingleShotConnection);
            return true;
        }
        if(process_->state()!=QProcess::Running)return false;
        qint64 const id=++nextId_;
        QJsonObject request{
            {"id",double(id)},{"method",method},
            {"text",source},{"position",position},{"language",language}
        };
        for(auto it=options.constBegin();it!=options.constEnd();++it)
            request.insert(it.key(),it.value());
        QByteArray payload=QJsonDocument(request).toJson(QJsonDocument::Compact);
        payload.append('\n');
        pending_.insert(id,std::move(callback));
        if(process_->write(payload)<0) {
            pending_.remove(id);
            return false;
        }
        return true;
    }
private:
    explicit TypeScriptCompletionService(QObject* parent):QObject(parent) {
        QString script=qEnvironmentVariable("NORTHSTARCPP_TS_SERVICE");
        if(script.isEmpty()) {
            QDir search(QCoreApplication::applicationDirPath());
            for(int i=0;i<9;++i) {
                QString const candidate=search.filePath(
                    "tools/ts_sdk/ts_language_service.cjs");
                if(QFileInfo::exists(candidate)) {
                    script=candidate;break;
                }
                if(!search.cdUp())break;
            }
        }
        QString const node=QStandardPaths::findExecutable("node");
        if(node.isEmpty() || script.isEmpty() ||
           !QFileInfo::exists(script)) return;
        process_=new QProcess(this);
        process_->setProgram(node);
        process_->setArguments({script});
        process_->setProcessChannelMode(QProcess::SeparateChannels);
        connect(process_,&QProcess::readyReadStandardOutput,this,[this] {
            buffer_.append(process_->readAllStandardOutput());
            if(buffer_.size()>4*1024*1024) {
                buffer_.clear();pending_.clear();return;
            }
            for(int newline=buffer_.indexOf('\n');newline>=0;
                newline=buffer_.indexOf('\n')) {
                QByteArray line=buffer_.left(newline);
                buffer_.remove(0,newline+1);
                auto doc=QJsonDocument::fromJson(line);
                if(!doc.isObject())continue;
                QJsonObject const reply=doc.object();
                qint64 const id=qint64(reply.value("id").toDouble());
                auto it=pending_.find(id);
                if(it==pending_.end())continue;
                Callback callback=std::move(it.value());
                pending_.erase(it);
                callback(reply);
            }
        });
        connect(process_,&QProcess::readyReadStandardError,this,[this] {
            process_->readAllStandardError(); // avoid unbounded pipe growth
        });
        connect(process_,QOverload<int,QProcess::ExitStatus>::of(
            &QProcess::finished),this,[this](int,QProcess::ExitStatus) {
            pending_.clear();
            buffer_.clear();
        });
        process_->start();
    }
    QProcess* process_=nullptr;
    QByteArray buffer_;
    QHash<qint64,Callback> pending_;
    qint64 nextId_=0;
};

class CodeEditor final:public QPlainTextEdit {
    class LineNumberArea final:public QWidget {
    public:
        explicit LineNumberArea(CodeEditor* editor):QWidget(editor),editor_(editor){}
        QSize sizeHint() const override {return {editor_->lineNumberWidth(),0};}
    protected:
        void paintEvent(QPaintEvent* event) override {editor_->paintNumbers(event);}
    private:
        CodeEditor* editor_;
    };
public:
    explicit CodeEditor(QWidget* parent=nullptr):QPlainTextEdit(parent) {
        gutter_=new LineNumberArea(this);
        gutter_->setObjectName("northstarLineNumbers");
        setObjectName("northstarTextAssetSource");
        setProperty("northstarLineRuling",true);
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(' ')*4);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setStyleSheet("QPlainTextEdit { background:#10151d; color:#e4e9ef;"
                      "border:1px solid #3b4758; selection-background-color:#335677; }");
        connect(this,&QPlainTextEdit::blockCountChanged,this,
                [this](int){updateNumbersWidth();});
        connect(this,&QPlainTextEdit::updateRequest,this,
                [this](QRect const& rect,int dy) {
            if(dy)gutter_->scroll(0,dy);
            else gutter_->update(0,rect.y(),gutter_->width(),rect.height());
            if(rect.contains(viewport()->rect()))updateNumbersWidth();
        });
        connect(this,&QPlainTextEdit::cursorPositionChanged,this,[this] {
            QList<QTextEdit::ExtraSelection> highlights;
            if(!isReadOnly()) {
                QTextEdit::ExtraSelection current;
                current.format.setBackground(QColor("#1c2b3a"));
                current.format.setProperty(QTextFormat::FullWidthSelection,true);
                current.cursor=textCursor();
                current.cursor.clearSelection();
                highlights.push_back(current);
            }
            for(auto const& value:diagnosticItems_) {
                QJsonObject const issue=value.toObject();
                int const offset=issue.value("start").toInt(-1);
                if(offset<0 || offset>=document()->characterCount())continue;
                QTextCursor marker(document());
                marker.setPosition(offset);
                marker.setPosition(std::min(offset+
                    issue.value("length").toInt(1),
                    document()->characterCount()-1),QTextCursor::KeepAnchor);
                QTextEdit::ExtraSelection highlight;
                highlight.cursor=marker;
                highlight.format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
                highlight.format.setUnderlineColor(
                    issue.value("category").toString()=="error"?
                        QColor("#ff626b"):QColor("#f2c26c"));
                highlight.format.setToolTip(issue.value("message").toString());
                highlights.push_back(highlight);
            }
            setExtraSelections(highlights);
        });
        updateNumbersWidth();
    }
    void enableNorthStarCompletion(Format format) {
        apiModel_=apiCompletionModel(this);
        tsModel_=new QStandardItemModel(this);
        tsLanguage_=format==Format::TypeScript?
            QStringLiteral("typescript"):QStringLiteral("javascript");
        completer_=new QCompleter(apiModel_,this);
        completer_->setWidget(this);
        completer_->setCompletionMode(QCompleter::PopupCompletion);
        completer_->setCaseSensitivity(Qt::CaseInsensitive);
        completer_->setFilterMode(Qt::MatchStartsWith);
        completer_->setMaxVisibleItems(12);
        completer_->popup()->setStyleSheet(
            "QListView{background:#202c3a;color:#f2f6fb;"
            "border:1px solid #c99b3b;selection-background-color:#315d80;}");
        connect(completer_,QOverload<QString const&>::of(&QCompleter::activated),
                this,[this](QString const& completion) {
            auto cursor=textCursor();
            if(completionFromTypeScript_) {
                cursor.movePosition(QTextCursor::Left,QTextCursor::KeepAnchor,
                                    activeTsFragment_.size());
                cursor.insertText(completion);
            }else{
                cursor.movePosition(QTextCursor::Left,QTextCursor::KeepAnchor,
                                    activePrefix_.size());
                QString const insertion=completionUsesRootAlias_?
                    activePrefix_.left(activePrefix_.indexOf('.')+1)+
                    completion.mid(QStringLiteral("UIRoot.").size()):completion;
                cursor.insertText(insertion);
            }
            setTextCursor(cursor);
        });
        tsTimer_=new QTimer(this);
        tsTimer_->setSingleShot(true);
        tsTimer_->setInterval(75);
        connect(tsTimer_,&QTimer::timeout,this,[this]{requestTsCompletion();});
        diagnosticTimer_=new QTimer(this);
        diagnosticTimer_->setSingleShot(true);
        diagnosticTimer_->setInterval(350);
        connect(diagnosticTimer_,&QTimer::timeout,this,[this]{requestDiagnostics();});
        connect(this,&QPlainTextEdit::textChanged,this,[this]{
            diagnosticTimer_->start();
        });
    }
    // Query the persistent TypeScript checker without blocking the UI thread.
    // Late results are ignored if the text or caret changed while parsing.
    void requestTsCompletion() {
        if(!completer_)return;
        auto const cursor=textCursor();
        int const position=cursor.position();
        int const revision=document()->revision();
        QString const line=cursor.block().text().left(cursor.positionInBlock());
        static QRegularExpression const word(R"(([A-Za-z_$][\w$]*)$)");
        auto const match=word.match(line);
        QString const fragment=match.hasMatch()?match.captured(1):QString{};
        QPointer<CodeEditor> const self(this);
        TypeScriptCompletionService::shared().complete(
            toPlainText(),position,tsLanguage_,fragment,
            [self,position,revision,fragment](QJsonObject const& reply) {
                if(!self || !reply.value("ok").toBool() ||
                   self->textCursor().position()!=position ||
                   self->document()->revision()!=revision)return;
                auto entries=reply.value("items").toArray();
                if(entries.isEmpty())return;
                auto* model=self->tsModel_;
                model->clear();
                QSet<QString> seen;
                for(auto const& value:entries) {
                    auto const item=value.toObject();
                    QString const name=item.value("name").toString();
                    if(name.isEmpty() || seen.contains(name))continue;
                    seen.insert(name);
                    auto* row=new QStandardItem(name);
                    QString const detail=item.value("detail").toString();
                    QString const docs=item.value("documentation").toString();
                    row->setToolTip(detail+(docs.isEmpty()?QString{}:
                        QString("\n")+docs));
                    model->appendRow(row);
                }
                if(!model->rowCount())return;
                self->completionFromTypeScript_=true;
                self->completionUsesRootAlias_=false;
                self->activeTsFragment_=fragment;
                self->completer_->setModel(model);
                self->completer_->setCompletionPrefix(fragment);
                if(!self->completer_->completionCount())return;
                QRect rect=self->cursorRect();
                rect.setWidth(std::max(260,
                    self->completer_->popup()->sizeHintForColumn(0)+35));
                self->completer_->complete(rect);
            });
    }
    void setDiagnosticSink(std::function<void(QJsonArray const&)> sink) {
        diagnosticSink_=std::move(sink);
        if(diagnosticTimer_)diagnosticTimer_->start();
    }
    void requestDiagnostics() {
        if(!completer_)return;
        int const revision=document()->revision();
        QPointer<CodeEditor> self(this);
        TypeScriptCompletionService::shared().request(
            "diagnostics",toPlainText(),textCursor().position(),tsLanguage_,
            QJsonObject{},
            [self,revision](QJsonObject const& reply) {
                if(!self || self->document()->revision()!=revision ||
                   !reply.value("ok").toBool())return;
                self->diagnosticItems_=reply.value("items").toArray();
                self->refreshDiagnosticSelections();
                if(self->diagnosticSink_)
                    self->diagnosticSink_(self->diagnosticItems_);
            });
    }
    // A single edit group means Ctrl+Z reverts an entire quick-fix/rename.
    bool applySemanticEdits(QJsonArray const& changes,int revision) {
        if(isReadOnly() || document()->revision()!=revision)return false;
        int const size=toPlainText().size();
        int previous=size+1;
        for(auto const& item:changes) {
            auto e=item.toObject();
            int const start=e.value("start").toInt(-1);
            int const length=e.value("length").toInt(-1);
            if(start<0 || length<0 || start+length>size ||
               start+length>previous)return false;
            previous=start;
        }
        QTextCursor cursor(document());
        cursor.beginEditBlock();
        for(auto const& item:changes) {
            auto e=item.toObject();
            cursor.setPosition(e.value("start").toInt());
            cursor.setPosition(e.value("start").toInt()+
                               e.value("length").toInt(),QTextCursor::KeepAnchor);
            cursor.insertText(e.value("text").toString());
        }
        cursor.endEditBlock();
        return true;
    }
    void requestQuickFix() {
        int const revision=document()->revision();
        int const position=textCursor().position();
        QPointer<CodeEditor> self(this);
        TypeScriptCompletionService::shared().request(
            "codeActions",toPlainText(),position,tsLanguage_,QJsonObject{},
            [self,revision](QJsonObject const& reply) {
                if(!self || self->document()->revision()!=revision)return;
                if(!reply.value("ok").toBool())return;
                auto const actions=reply.value("items").toArray();
                if(actions.isEmpty()) {
                    self->setToolTip("No safe TypeScript fix for this diagnostic");
                    return;
                }
                if(actions.size()>1) {
                    // Never enter a nested Qt event loop for code actions.
                    auto* menu=new QMenu(self);
                    for(int i=0;i<actions.size();++i) {
                        auto* option=menu->addAction(actions[i].toObject()
                                                   .value("title").toString());
                        option->setData(i);
                    }
                    connect(menu,&QMenu::triggered,self,
                        [self,actions,revision](QAction* selected) {
                            if(!self || !selected)return;
                            int const chosen=selected->data().toInt();
                            if(chosen>=0 && chosen<actions.size())
                                self->applySemanticEdits(actions[chosen].toObject()
                                    .value("edits").toArray(),revision);
                        });
                    connect(menu,&QMenu::aboutToHide,menu,&QObject::deleteLater);
                    menu->popup(self->mapToGlobal(
                        self->cursorRect().bottomLeft()));
                    return;
                }
                self->applySemanticEdits(
                    actions[0].toObject().value("edits").toArray(),revision);
            });
    }
    void requestRename() {
        if(isReadOnly())return;
        bool confirmed=false;
        QString const replacement=QInputDialog::getText(this,"Rename symbol",
            "New identifier:",QLineEdit::Normal,{},&confirmed);
        if(!confirmed || replacement.trimmed().isEmpty())return;
        int const revision=document()->revision();
        int const position=textCursor().position();
        QPointer<CodeEditor> self(this);
        TypeScriptCompletionService::shared().request(
            "rename",toPlainText(),position,tsLanguage_,
            QJsonObject{{"newName",replacement}},
            [self,revision](QJsonObject const& reply) {
                if(!self || self->document()->revision()!=revision)return;
                if(!reply.value("ok").toBool()) {
                    QMessageBox::information(self,"Rename",
                        reply.value("error").toString());return;
                }
                auto edits=reply.value("items").toArray();
                if(QMessageBox::question(self,"Rename symbol",
                    QString("Rename %1 occurrence(s) of '%2' in this file?")
                        .arg(edits.size()).arg(reply.value("symbol").toString()),
                    QMessageBox::Yes|QMessageBox::No,QMessageBox::No)
                    !=QMessageBox::Yes)return;
                self->applySemanticEdits(edits,revision);
            });
    }
    void requestFormat() {
        if(isReadOnly())return;
        int const revision=document()->revision();
        QPointer<CodeEditor> self(this);
        TypeScriptCompletionService::shared().request(
            "format",toPlainText(),textCursor().position(),tsLanguage_,
            QJsonObject{},
            [self,revision](QJsonObject const& reply) {
                if(!self || self->document()->revision()!=revision ||
                   !reply.value("ok").toBool())return;
                self->applySemanticEdits(reply.value("items").toArray(),revision);
            });
    }
    void refreshDiagnosticSelections() {
        QList<QTextEdit::ExtraSelection> selections;
        QTextEdit::ExtraSelection active;
        active.cursor=textCursor();active.cursor.clearSelection();
        active.format.setBackground(QColor("#1c2b3a"));
        active.format.setProperty(QTextFormat::FullWidthSelection,true);
        selections.push_back(active);
        for(auto const& value:diagnosticItems_) {
            auto issue=value.toObject();
            int const start=issue.value("start").toInt(-1);
            if(start<0 || start>=document()->characterCount())continue;
            QTextCursor marker(document());
            marker.setPosition(start);
            marker.setPosition(std::min(start+issue.value("length").toInt(1),
                document()->characterCount()-1),QTextCursor::KeepAnchor);
            QTextEdit::ExtraSelection selection;
            selection.cursor=marker;
            selection.format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            selection.format.setUnderlineColor(
                issue.value("category").toString()=="error"?
                    QColor("#ff626b"):QColor("#f2c26c"));
            selection.format.setToolTip(issue.value("message").toString());
            selections.push_back(selection);
        }
        setExtraSelections(selections);
    }
protected:
    bool event(QEvent* event) override {
        // Qt Editor also binds Ctrl+Space to Content Drawer. When the code
        // editor has focus, reserve that chord for API completion.
        if(completer_ && event->type()==QEvent::ShortcutOverride) {
            auto* key=static_cast<QKeyEvent*>(event);
            if(key->modifiers()==Qt::ControlModifier &&
               key->key()==Qt::Key_Space) {
                event->accept();
                return true;
            }
        }
        return QPlainTextEdit::event(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if(completer_ && completer_->popup()->isVisible()) {
            switch(event->key()) {
            case Qt::Key_Enter:case Qt::Key_Return:case Qt::Key_Escape:
            case Qt::Key_Tab:case Qt::Key_Backtab:
                event->ignore();return; // handled by completion popup
            default:break;
            }
        }
        bool const explicitComplete=(event->modifiers()==Qt::ControlModifier &&
                                     event->key()==Qt::Key_Space);
        if(!explicitComplete)QPlainTextEdit::keyPressEvent(event);
        if(completer_ &&
           (explicitComplete || event->text()=="." || event->text()==":" ||
            event->text()=="\"" || event->text()=="'" ||
            (!event->text().isEmpty() &&
             (event->text().front().isLetterOrNumber() ||
              event->text()=="_")))) {
            if(completer_->model()!=apiModel_)
                completer_->setModel(apiModel_);
            completionFromTypeScript_=false;
            tsTimer_->start();
            auto const cursor=textCursor();
            QString const input=cursor.block().text().left(cursor.positionInBlock());
            static QRegularExpression const word(
                R"(([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*\.?|\b[A-Za-z_$][\w$]*)$)");
            auto match=word.match(input);
            QString const prefix=match.hasMatch()?match.captured(1):QString{};
            if(prefix.size()<2 && !explicitComplete) {
                completer_->popup()->hide();
                return;
            }
            if(!prefix.isEmpty() || explicitComplete) {
                activePrefix_=prefix;
                completionUsesRootAlias_=false;
                QString lookup=prefix;
                auto const dot=prefix.indexOf('.');
                if(dot>0) {
                    // Infer UI root variables from authored declarations.
                    // Completions always come from embedded TS declarations.
                    static QRegularExpression const rootDeclaration(
                        R"re(\b(?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*(?::\s*UIRoot\s*)?=\s*UI\.(?:createRoot|createSurface|loadAsset)\s*\()re");
                    QString const scope=toPlainText().left(cursor.position());
                    auto it=rootDeclaration.globalMatch(scope);
                    while(it.hasNext())
                        if(it.next().captured(1)==prefix.left(dot))
                            completionUsesRootAlias_=true;
                    if(completionUsesRootAlias_)
                        lookup=QStringLiteral("UIRoot.")+prefix.mid(dot+1);
                }
                completer_->setCompletionPrefix(lookup);
                if(completer_->completionCount()>0) {
                    QRect rect=cursorRect();
                    rect.setWidth(std::max(260,completer_->popup()->sizeHintForColumn(0)+35));
                    completer_->complete(rect);
                }else completer_->popup()->hide();
            }
        } else if(completer_)completer_->popup()->hide();
    }
    void paintEvent(QPaintEvent* event) override {
        QPlainTextEdit::paintEvent(event);
        // Draw only visible row separators: O(visible blocks), not O(document).
        QPainter painter(viewport());
        painter.setPen(QColor("#273543"));
        auto block=firstVisibleBlock();
        int top=qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        while(block.isValid() && top<=event->rect().bottom()) {
            int const bottom=top+qRound(blockBoundingRect(block).height());
            int const y=bottom-1;
            if(block.isVisible() && y>=event->rect().top() && y<=event->rect().bottom())
                painter.drawLine(0,y,viewport()->width()-1,y);
            block=block.next();
            top=bottom;
        }
    }
    void changeEvent(QEvent* event) override {
        QPlainTextEdit::changeEvent(event);
        if(!gutter_)return;
        if(event->type()==QEvent::FontChange ||
           event->type()==QEvent::ApplicationFontChange ||
           event->type()==QEvent::StyleChange) {
            // Qt DPI and user font changes invalidate both tab stops and the
            // line-number gutter; its width is measured in logical pixels.
            setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(' ')*4);
            updateNumbersWidth();
            updateGutterGeometry();
            gutter_->update();
        }
    }
    void resizeEvent(QResizeEvent* event) override {
        QPlainTextEdit::resizeEvent(event);
        updateGutterGeometry();
    }
private:
    int lineNumberWidth() const {
        int digits=1;
        for(int number=std::max(1,blockCount());number>=10;number/=10)++digits;
        return 14+fontMetrics().horizontalAdvance(QLatin1Char('9'))*digits;
    }
    void updateNumbersWidth() {
        setViewportMargins(lineNumberWidth(),0,0,0);
        if(gutter_)gutter_->update();
    }
    void updateGutterGeometry() {
        if(!gutter_)return;
        auto const rect=contentsRect();
        gutter_->setGeometry(rect.left(),rect.top(),
                             lineNumberWidth(),rect.height());
    }
    void paintNumbers(QPaintEvent* event) {
        QPainter painter(gutter_);
        painter.fillRect(event->rect(),QColor("#17202b"));
        int const current=textCursor().blockNumber();
        auto block=firstVisibleBlock();
        int number=block.blockNumber();
        int top=qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom=top+qRound(blockBoundingRect(block).height());
        while(block.isValid() && top<=event->rect().bottom()) {
            if(block.isVisible() && bottom>=event->rect().top()) {
                if(number==current)
                    painter.fillRect(0,top,gutter_->width(),fontMetrics().height(),
                                     QColor("#273b50"));
                painter.setPen(number==current?QColor("#ffcf65"):QColor("#8495ab"));
                painter.drawText(0,top,gutter_->width()-6,fontMetrics().height(),
                                 Qt::AlignRight,QString::number(number+1));
            }
            block=block.next();++number;
            top=bottom;
            bottom=top+qRound(blockBoundingRect(block).height());
        }
    }
    LineNumberArea* gutter_=nullptr;
    QCompleter* completer_=nullptr;
    QStandardItemModel* apiModel_=nullptr;
    QStandardItemModel* tsModel_=nullptr;
    QTimer* tsTimer_=nullptr;
    QTimer* diagnosticTimer_=nullptr;
    QJsonArray diagnosticItems_;
    std::function<void(QJsonArray const&)> diagnosticSink_;
    QString activePrefix_;
    QString activeTsFragment_;
    QString tsLanguage_;
    bool completionFromTypeScript_=false;
    bool completionUsesRootAlias_=false;
};

class CodeHighlighter final:public QSyntaxHighlighter {
public:
    CodeHighlighter(QTextDocument* document,Format format)
        :QSyntaxHighlighter(document),format_(format) {}
protected:
    void highlightBlock(QString const& source) override {
        if(format_==Format::Plain)return;
        if(format_==Format::Markdown) {
            static QRegularExpression const headings(R"(^\\s{0,3}#{1,6}\\s+.*$)");
            static QRegularExpression const inlineCode(R"re(`[^`]+`)re");
            static QRegularExpression const emphasis(R"(\\*\\*[^*]+\\*\\*)");
            static QRegularExpression const links(R"(\\[[^\\]]+\\]\\([^)]*\\))");
            auto applyMd=[&](QRegularExpression const& expression,QColor const& color) {
                QTextCharFormat fmt;fmt.setForeground(color);
                auto matches=expression.globalMatch(source);
                while(matches.hasNext()) {
                    auto match=matches.next();
                    setFormat(match.capturedStart(),match.capturedLength(),fmt);
                }
            };
            applyMd(emphasis,QColor("#e1b773"));
            applyMd(links,QColor("#90c6fa"));
            applyMd(inlineCode,QColor("#a6d8a9"));
            applyMd(headings,QColor("#ffcf65"));
            return;
        }
        auto apply=[&](QRegularExpression const& pattern,QColor color) {
            auto it=pattern.globalMatch(source);
            QTextCharFormat text;
            text.setForeground(color);
            while(it.hasNext()) {
                auto match=it.next();
                setFormat(match.capturedStart(),match.capturedLength(),text);
            }
        };
        static QRegularExpression const numbers(
            R"(\b(?:0x[0-9a-fA-F]+|\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\b)");
        static QRegularExpression const strings(
            R"re("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')re");
        static QRegularExpression const templateStrings(
            R"re(`(?:\\\\.|[^`\\\\])*`)re");
        static QRegularExpression const jsonKeys(
            R"re("(?:\\\\.|[^"\\\\])*"\\s*(?=:))re");
        static QRegularExpression const jsKeywords(
            R"(\b(?:async|await|break|case|catch|class|const|continue|debugger|default|delete|do|else|export|extends|false|finally|for|from|function|if|import|in|instanceof|let|new|null|of|return|static|super|switch|this|throw|true|try|typeof|undefined|var|void|while|yield)\b)");
        static QRegularExpression const glslKeywords(
            R"(\b(?:attribute|bool|break|buffer|case|const|continue|discard|do|double|else|false|float|for|highp|if|in|inout|int|layout|lowp|mat2|mat3|mat4|mediump|out|precision|return|sampler2D|samplerCube|struct|switch|true|uint|uniform|varying|vec2|vec3|vec4|void|while)\b)");
        static QRegularExpression const glslPreprocessor(R"(^\s*#\s*\w+.*$)");
        static QRegularExpression const lineComment(R"(//[^\n]*)");
        apply(numbers,QColor("#e5b971"));
        bool const js=format_==Format::JavaScript || format_==Format::TypeScript;
        bool const json=format_==Format::Json;
        if(!json)apply(js?jsKeywords:glslKeywords,QColor("#ffcf65"));
        else {
            static QRegularExpression const jsonLiterals(R"(\\b(?:true|false|null)\\b)");
            apply(jsonLiterals,QColor("#ffcf65"));
        }
        apply(strings,QColor("#9bd9a6"));
        if(json) {
            apply(jsonKeys,QColor("#8ac8ed"));
            return; // JSON does not allow JS comments or GLSL preprocessors
        }
        if(js)apply(templateStrings,QColor("#9bd9a6"));
        if(!js)apply(glslPreprocessor,QColor("#be9bdb"));
        apply(lineComment,QColor("#778b9a"));
        // Multi-line C-style comments can span independent QTextBlocks.
        static QRegularExpression const commentStart(R"(/\*)");
        static QRegularExpression const commentEnd(R"(\*/)");
        QTextCharFormat comments;
        comments.setForeground(QColor("#778b9a"));
        setCurrentBlockState(0);
        int start=previousBlockState()==1?0:source.indexOf(commentStart);
        while(start>=0) {
            auto match=commentEnd.match(source,start+(previousBlockState()==1?0:2));
            int const end=match.hasMatch()?match.capturedEnd():source.size();
            setFormat(start,end-start,comments);
            if(!match.hasMatch()) {
                setCurrentBlockState(1);
                break;
            }
            start=source.indexOf(commentStart,end);
        }
    }
private:
    Format format_;
};

// One source widget for JS/TS, GLSL, JSON, Markdown and read-only API assets.
// Parent document owns its highlighter; no duplicate editor implementations.
CodeEditor* makeCodeEditor(QWidget* parent,Format format,bool readOnly) {
    auto* source=new CodeEditor(parent);
    source->setReadOnly(readOnly);
    if(!readOnly && (format==Format::JavaScript || format==Format::TypeScript))
        source->enableNorthStarCompletion(format);
    new CodeHighlighter(source->document(),format);
    return source;
}

class TextAssetDialog final:public QDialog {
public:
    TextAssetDialog(QWidget* parent,QString path,Format kind,bool shared)
        :QDialog(parent),path_(std::move(path)),format_(kind),
         readOnly_(shared || kind==Format::Markdown) {
        setObjectName("northstarTextAssetDialog");
        setWindowTitle(QFileInfo(path_).fileName()+" — "+formatTitle(kind));
        resize(1060,720);
        setMinimumSize(600,420);
        setStyleSheet("QDialog {background:#151b25;color:#dce5ef;}"
                      "QLabel{color:#aebfd1;}"
                      "QPushButton{background:#273646;color:#e4edf6;border:1px solid #53647b;"
                      "border-radius:4px;padding:6px 14px;}"
                      "QPushButton:hover{background:#344d63;}"
                      "QTabWidget::pane{border:1px solid #37485b;}"
                      "QTabBar::tab{background:#243343;color:#d9e5ee;padding:8px 17px;}"
                      "QTabBar::tab:selected{background:#354c63;}");
        auto* layout=new QVBoxLayout(this);
        auto* heading=new QLabel(QFileInfo(path_).fileName()+"  |  "+formatTitle(kind),this);
        layout->addWidget(heading);
        tabs_=new QTabWidget(this);
        source_=makeCodeEditor(tabs_,kind,readOnly_);
        if(kind==Format::Markdown) {
            preview_=new QTextBrowser(tabs_);
            preview_->setObjectName("northstarMarkdownPreview");
            preview_->setOpenLinks(false);
            preview_->setOpenExternalLinks(false);
            preview_->setStyleSheet("QTextBrowser{background:#111923;color:#e6ebf2;"
                                   "border:0;padding:18px;}");
            preview_->document()->setDefaultStyleSheet(
                "h1,h2,h3{color:#ffcd52} a{color:#85c6ff}"
                "pre,code{background:#263240;color:#f1ca7e}"
                "blockquote{border-left:3px solid #dba944;padding-left:12px}"
                "table{border-collapse:collapse}td,th{border:1px solid #637286;padding:5px}");
            preview_->document()->setBaseUrl(QUrl::fromLocalFile(
                QFileInfo(path_).absolutePath()+"/"));
            tabs_->addTab(preview_,"Preview");
            tabs_->addTab(source_,"Markdown Source");
        } else {
            tabs_->addTab(source_,"Source");

        }
        layout->addWidget(tabs_,1);
        // Search/replace is local to this document, not the scene or asset grid.
        auto* searchRow=new QHBoxLayout;
        find_=new QLineEdit(this);
        find_->setObjectName("textAssetFind");
        find_->setPlaceholderText("Find… (Ctrl+F)");
        replace_=new QLineEdit(this);
        replace_->setObjectName("textAssetReplace");
        replace_->setPlaceholderText("Replace… (Ctrl+H)");
        searchRow->addWidget(find_,3);
        searchRow->addWidget(replace_,3);
        auto* next=new QPushButton("Find Next",this);
        next->setObjectName("textAssetFindNext");
        auto* one=new QPushButton("Replace",this);
        one->setObjectName("textAssetReplaceOne");
        auto* all=new QPushButton("Replace All",this);
        all->setObjectName("textAssetReplaceAll");
        searchRow->addWidget(next);
        searchRow->addWidget(one);
        searchRow->addWidget(all);
        one->setEnabled(!readOnly_);
        all->setEnabled(!readOnly_);
        layout->addLayout(searchRow);
        connect(next,&QPushButton::clicked,this,[this]{findNext();});
        connect(find_,&QLineEdit::returnPressed,this,[this]{findNext();});
        connect(one,&QPushButton::clicked,this,[this]{replaceOne();});
        connect(all,&QPushButton::clicked,this,[this]{replaceAll();});
        auto* findShortcut=new QShortcut(QKeySequence::Find,source_);
        findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(findShortcut,&QShortcut::activated,this,[this]{
            find_->setFocus();find_->selectAll();
        });
        auto* replaceShortcut=new QShortcut(QKeySequence("Ctrl+H"),source_);
        replaceShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(replaceShortcut,&QShortcut::activated,this,[this]{
            replace_->setFocus();replace_->selectAll();
        });
        if(kind==Format::VertexShader || kind==Format::FragmentShader) {
            auto* validate=new QPushButton("Validate GLSL (glslc)",this);
            validate->setObjectName("textAssetValidate");
            layout->addWidget(validate);
            diagnostics_=new QListWidget(this);
            diagnostics_->setObjectName("textAssetDiagnostics");
            diagnostics_->setMaximumHeight(112);
            diagnostics_->setStyleSheet("QListWidget{background:#151b25;"
                                        "color:#e0d1b2;border:1px solid #38485a;}");
            layout->addWidget(diagnostics_);
            connect(validate,&QPushButton::clicked,this,[this] {
                validateShader(false);
            });
            connect(diagnostics_,&QListWidget::itemActivated,this,
                    [this](QListWidgetItem* item) {
                int const line=item->data(Qt::UserRole).toInt();
                if(line<=0)return;
                auto block=source_->document()->findBlockByNumber(line-1);
                if(!block.isValid())return;
                QTextCursor cursor(block);
                source_->setTextCursor(cursor);
                source_->centerCursor();
                source_->setFocus();
            });
        }
        if((kind==Format::JavaScript || kind==Format::TypeScript) &&
           !readOnly_) {
            auto* languageTools=new QHBoxLayout;
            auto* check=new QPushButton("Check Types / Syntax",this);
            check->setObjectName("textAssetTypeCheck");
            auto* fix=new QPushButton("Quick Fix (Ctrl+.)",this);
            fix->setObjectName("textAssetQuickFix");
            auto* rename=new QPushButton("Rename Symbol (F2)",this);
            rename->setObjectName("textAssetRenameSymbol");
            auto* format=new QPushButton("Format (Ctrl+Shift+I)",this);
            format->setObjectName("textAssetFormat");
            languageTools->addWidget(check);
            languageTools->addWidget(fix);
            languageTools->addWidget(rename);
            languageTools->addWidget(format);
            layout->addLayout(languageTools);
            diagnostics_=new QListWidget(this);
            diagnostics_->setObjectName("textAssetDiagnostics");
            diagnostics_->setMaximumHeight(130);
            diagnostics_->setStyleSheet(
                "QListWidget{background:#141f2a;color:#e7daca;"
                "border:1px solid #38485a;}");
            layout->addWidget(diagnostics_);
            source_->setDiagnosticSink([this](QJsonArray const& issues) {
                diagnostics_->clear();
                for(auto const& value:issues) {
                    QJsonObject const item=value.toObject();
                    QString const severity=item.value("category").toString();
                    int const line=item.value("line").toInt();
                    QString const description=
                        QString("%1:%2  TS%3 [%4] %5")
                          .arg(line).arg(item.value("column").toInt())
                          .arg(item.value("code").toInt())
                          .arg(severity,item.value("message").toString());
                    auto* row=new QListWidgetItem(description,diagnostics_);
                    row->setData(Qt::UserRole,line);
                    row->setForeground(severity=="error"?
                        QColor("#ff858c"):QColor("#e5bd78"));
                    row->setToolTip(item.value("message").toString());
                }
                if(issues.isEmpty())
                    new QListWidgetItem("No TypeScript/JavaScript issues",
                                        diagnostics_);
            });
            connect(check,&QPushButton::clicked,source_,[this] {
                source_->requestDiagnostics();
            });
            connect(fix,&QPushButton::clicked,source_,[this] {
                source_->requestQuickFix();
            });
            connect(rename,&QPushButton::clicked,source_,[this] {
                source_->requestRename();
            });
            connect(format,&QPushButton::clicked,source_,[this] {
                source_->requestFormat();
            });
            auto* fixShortcut=new QShortcut(QKeySequence("Ctrl+."),source_);
            connect(fixShortcut,&QShortcut::activated,source_,[this]{
                source_->requestQuickFix();
            });
            auto* renameShortcut=new QShortcut(QKeySequence(Qt::Key_F2),source_);
            connect(renameShortcut,&QShortcut::activated,source_,[this]{
                source_->requestRename();
            });
            auto* formatShortcut=new QShortcut(
                QKeySequence("Ctrl+Shift+I"),source_);
            connect(formatShortcut,&QShortcut::activated,source_,[this]{
                source_->requestFormat();
            });
            connect(diagnostics_,&QListWidget::itemActivated,source_,
                [this](QListWidgetItem* item) {
                    int const line=item->data(Qt::UserRole).toInt();
                    if(line<=0)return;
                    auto block=source_->document()->findBlockByNumber(line-1);
                    if(!block.isValid())return;
                    QTextCursor cursor(block);
                    source_->setTextCursor(cursor);
                    source_->centerCursor();
                    source_->setFocus();
                });
        }
        status_=new QLabel(this);
        layout->addWidget(status_);
        auto* actions=new QHBoxLayout;
        auto* reload=new QPushButton("Reload",this);
        reload->setObjectName("textAssetReload");
        actions->addWidget(reload);
        actions->addStretch();
        if(!readOnly_) {
            save_=new QPushButton("Save  (Ctrl+S)",this);
            save_->setObjectName("textAssetSave");
            actions->addWidget(save_);
            connect(save_,&QPushButton::clicked,this,[this]{save();});
            save_->setEnabled(false);
            connect(source_->document(),&QTextDocument::modificationChanged,
                    this,[this](bool dirty) {
                if(save_)save_->setEnabled(dirty);
                updateStatus();
            });
        }
        close_=new QPushButton("Close",this);
        actions->addWidget(close_);
        layout->addLayout(actions);
        connect(reload,&QPushButton::clicked,this,[this] {reloadFile(true);});
        connect(close_,&QPushButton::clicked,this,&QDialog::reject);
        if(!readOnly_) {
            auto* shortcut=new QShortcut(QKeySequence::Save,this);
            shortcut->setContext(Qt::WidgetWithChildrenShortcut);
            connect(shortcut,&QShortcut::activated,this,[this]{save();});
        }
        reloadFile(false);
    }
    bool canClose() const {
        if(readOnly_ || !source_->document()->isModified())return true;
        return QMessageBox::question(const_cast<TextAssetDialog*>(this),"Unsaved Changes",
            "Discard changes to "+QFileInfo(path_).fileName()+"?",
            QMessageBox::Discard|QMessageBox::Cancel,
            QMessageBox::Cancel)==QMessageBox::Discard;
    }
    bool dirty() const {return source_->document()->isModified();}
    QString path() const {return path_;}
    void setEmbedded(std::function<void(QString const&,bool)> callback,
                     std::function<void(QString const&,QByteArray const&)> compiled) {
        setWindowFlags(Qt::Widget);
        setMinimumSize(0,0); // Respect dock resizing in embedded mode.
        if(close_)close_->hide();
        savedCallback_=std::move(callback);
        shaderCompiledCallback_=std::move(compiled);
    }
    void reject() override {
        if(canClose())QDialog::reject();
    }
private:
    bool findNext() {
        QString const needle=find_->text();
        if(needle.isEmpty())return false;
        // QTextDocument search is literal by default; wrap once at EOF.
        if(!source_->find(needle)) {
            auto cursor=source_->textCursor();
            cursor.movePosition(QTextCursor::Start);
            source_->setTextCursor(cursor);
            if(!source_->find(needle)) {
                status_->setText("No matches: "+needle);
                return false;
            }
        }
        source_->setFocus();
        return true;
    }
    void replaceOne() {
        if(readOnly_ || find_->text().isEmpty())return;
        auto cursor=source_->textCursor();
        if(!cursor.hasSelection() ||
           cursor.selectedText().compare(find_->text(),Qt::CaseInsensitive)!=0) {
            findNext();
            cursor=source_->textCursor();
        }
        if(cursor.hasSelection())cursor.insertText(replace_->text());
        source_->setTextCursor(cursor);
        findNext();
    }
    void replaceAll() {
        if(readOnly_ || find_->text().isEmpty())return;
        QTextCursor cursor(source_->document());
        cursor.beginEditBlock();
        cursor.movePosition(QTextCursor::Start);
        int changes=0;
        while(true) {
            auto match=source_->document()->find(find_->text(),cursor);
            if(match.isNull())break;
            match.insertText(replace_->text());
            cursor=match;
            ++changes;
        }
        cursor.endEditBlock();
        status_->setText(QString("Replaced %1 occurrence(s)").arg(changes));
    }
    QString compilerPath() const {
        if(QFileInfo(path_).suffix().compare("hlsl",Qt::CaseInsensitive)==0)
            return QStandardPaths::findExecutable("dxc");
        QString path=QStandardPaths::findExecutable("glslc");
        if(!path.isEmpty())return path;
        auto const sdk=qEnvironmentVariable("VULKAN_SDK");
        if(sdk.isEmpty())return {};
        QString const exe=QDir(sdk).filePath(
#ifdef Q_OS_WIN
            "Bin/glslc.exe"
#else
            "bin/glslc"
#endif
        );
        return QFileInfo::exists(exe)?exe:QString{};
    }
    void addDiagnostic(QString const& message,int line=0) {
        if(!diagnostics_)return;
        auto* item=new QListWidgetItem(message,diagnostics_);
        item->setData(Qt::UserRole,line);
        if(line>0)item->setToolTip("Double-click to go to line "+QString::number(line));
    }
    void validateShader(bool notifySaved) {
        if(!diagnostics_)return;
        if(validation_) {
            validation_->disconnect(this);
            validation_->kill();
            validation_->deleteLater();
            validation_=nullptr;
        }
        pendingReload_=notifySaved;
        diagnostics_->clear();
        QString const compiler=compilerPath();
        if(compiler.isEmpty()) {
            pendingReload_=false;
            addDiagnostic("Shader compiler missing: install glslc (Vulkan) or dxc (DXIL). "
                          "Shader saved, GPU reload skipped.");
            return;
        }
        temp_=std::make_unique<QTemporaryDir>();
        if(!temp_->isValid()) {
            pendingReload_=false;
            addDiagnostic("Failed to create temporary shader validation directory.");
            return;
        }
        bool const hlsl=QFileInfo(path_).suffix().compare("hlsl",Qt::CaseInsensitive)==0;
        QString const stage=format_==Format::VertexShader?"vert":"frag";
        QString const input=temp_->filePath(hlsl?"validation.hlsl":"validation."+stage);
        QFile file(input);
        if(!file.open(QIODevice::WriteOnly)) {
            pendingReload_=false;
            addDiagnostic("Unable to create temporary GLSL source.");
            return;
        }
        auto const bytes=source_->toPlainText().toUtf8();
        if(file.write(bytes)!=bytes.size()) {
            pendingReload_=false;
            addDiagnostic("Unable to write temporary GLSL source.");
            return;
        }
        file.close();
        validation_=new QProcess(this);
        validation_->setProcessChannelMode(QProcess::MergedChannels);
        validation_->setProgram(compiler);
        if(hlsl) {
            auto const name=QFileInfo(path_).fileName().toLower();
            QString entry="material_ps",profile="ps_6_0";
            if(name.endsWith(".vs.hlsl") || name.endsWith(".vertex.hlsl")) {
                entry="material_vs";profile="vs_6_0";
            } else if(name.endsWith(".gbuffer.hlsl"))entry="material_gbuffer_ps";
            validation_->setArguments({"-T",profile,"-E",entry,
                "-I",QFileInfo(path_).absolutePath(),
                "-Fo",temp_->filePath("validated.dxil"),input});
        }else validation_->setArguments({"-fshader-stage="+stage,
            "--target-env=vulkan1.2",
            "-I"+QFileInfo(path_).absolutePath(),
            "-o",temp_->filePath("validated.spv"),input});
        connect(validation_,&QProcess::finished,this,
            [this](int exitCode,QProcess::ExitStatus exitStatus) {
            if(!validation_)return;
            QString const output=QString::fromUtf8(validation_->readAll());
            static QRegularExpression const linePattern(
                R"((?::|^)(\d+)(?::\d+)?:\s*(?:error|warning)[:\s])",
                QRegularExpression::CaseInsensitiveOption);
            for(QString const& raw:output.split('\n',Qt::SkipEmptyParts)) {
                auto const match=linePattern.match(raw);
                addDiagnostic(raw.trimmed(),match.hasMatch()?match.captured(1).toInt():0);
            }
            bool const success=exitStatus==QProcess::NormalExit && exitCode==0;
            if(success) {
                bool const dxil=QFileInfo(path_).suffix().compare("hlsl",Qt::CaseInsensitive)==0;
                addDiagnostic(dxil?"HLSL valid: DXC produced DXIL.":
                                   "GLSL valid: glslc produced SPIR-V.");
                if(pendingReload_ && shaderCompiledCallback_) {
                    QFile compiled(temp_->filePath(dxil?"validated.dxil":"validated.spv"));
                    if(compiled.open(QIODevice::ReadOnly)) {
                        auto spirv=compiled.readAll();
                        if(spirv.size()>=24 &&
                           (dxil?spirv.startsWith("DXBC"):
                             (spirv.size()%4==0 &&
                              spirv.startsWith(QByteArray::fromHex("03022307")))))
                            shaderCompiledCallback_(path_,spirv);
                        else addDiagnostic("Compiled SPIR-V payload is invalid.");
                    }else addDiagnostic("Cannot read compiled SPIR-V.");
                }
                if(pendingReload_ && savedCallback_)savedCallback_(path_,true);
            } else if(output.trimmed().isEmpty())
                addDiagnostic("glslc failed without diagnostic output.");
            pendingReload_=false;
            validation_->deleteLater();
            validation_=nullptr;
            temp_.reset();
        });
        connect(validation_,&QProcess::errorOccurred,this,
            [this](QProcess::ProcessError error) {
            if(error==QProcess::FailedToStart && validation_) {
                pendingReload_=false;
                addDiagnostic("glslc could not start: "+validation_->errorString());
            }
        });
        validation_->start();
    }
    bool reloadFile(bool interactive) {
        if(interactive && !readOnly_ && source_->document()->isModified()) {
            if(QMessageBox::question(this,"Reload Asset",
                "Discard unsaved changes and reload from disk?",
                QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)
                return false;
        }
        try {
            auto loaded=readFile(path_);
            baseline_=std::move(loaded.original);
            bom_=loaded.bom;
            crlf_=loaded.crlf;
            source_->setPlainText(loaded.source);
            source_->document()->setModified(false);
            if(preview_)preview_->setMarkdown(loaded.source);
            updateStatus();
            return true;
        } catch(std::exception const& error) {
            QMessageBox::warning(this,"Text Asset",QString::fromUtf8(error.what()));
            return false;
        }
    }
    bool save() {
        if(readOnly_)return false;
        try {
            // Refuse a silent overwrite when a tool or another editor changed this file.
            auto current=readFile(path_);
            if(current.original!=baseline_) {
                QMessageBox::warning(this,"External Changes",
                    "The file changed on disk. Copy your changes elsewhere or "
                    "review them before reloading; no data was overwritten.");
                return false;
            }
            QString text=source_->toPlainText();
            if(crlf_)text.replace("\n","\r\n");
            QByteArray bytes=text.toUtf8();
            if(bom_)bytes.prepend("\xEF\xBB\xBF");
            if(bytes.size()>max_source_bytes) {
                QMessageBox::warning(this,"Text Asset","File exceeds the 8 MiB limit.");
                return false;
            }
            QSaveFile file(path_);
            if(!file.open(QIODevice::WriteOnly))
                throw std::runtime_error(("Cannot save: "+file.errorString()).toUtf8().toStdString());
            if(file.write(bytes)!=bytes.size() || !file.commit())
                throw std::runtime_error(("Save failed: "+file.errorString()).toUtf8().toStdString());
            baseline_=std::move(bytes);
            source_->document()->setModified(false);
            updateStatus();
            if(format_==Format::VertexShader || format_==Format::FragmentShader)
                validateShader(true);
            else if(savedCallback_)savedCallback_(path_,false);
            return true;
        } catch(std::exception const& error) {
            QMessageBox::critical(this,"Text Asset",QString::fromUtf8(error.what()));
            return false;
        }
    }
    void updateStatus() {
        QString const mode=readOnly_?"Read-only":(source_->document()->isModified()?"Modified":"Saved");
        status_->setText(mode+"  |  UTF-8  |  "+(crlf_?"CRLF":"LF")+"  |  "+path_);
    }
    QString path_;
    Format format_;
    bool readOnly_=true,bom_=false,crlf_=false;
    QByteArray baseline_;
    QTabWidget* tabs_=nullptr;
    CodeEditor* source_=nullptr;
    QTextBrowser* preview_=nullptr;
    QLabel* status_=nullptr;
    QPushButton* save_=nullptr;
    QPushButton* close_=nullptr;
    QLineEdit* find_=nullptr;
    QLineEdit* replace_=nullptr;
    QListWidget* diagnostics_=nullptr;
    QProcess* validation_=nullptr;
    std::unique_ptr<QTemporaryDir> temp_;
    bool pendingReload_=false;
    std::function<void(QString const&,bool)> savedCallback_;
    std::function<void(QString const&,QByteArray const&)> shaderCompiledCallback_;

};
} // namespace

Format formatFor(QString const& filename) noexcept {
    QString const suffix=QFileInfo(filename).suffix().toLower();
    if(suffix=="md" || suffix=="markdown")return Format::Markdown;
    if(suffix=="js" || suffix=="mjs" || suffix=="cjs" ||
       suffix=="jsx")return Format::JavaScript;
    if(suffix=="vert")return Format::VertexShader;
    if(suffix=="frag" || suffix=="hlsl")return Format::FragmentShader;
    if(suffix=="ts" || suffix=="tsx" || suffix=="ysc")return Format::TypeScript;
    if(suffix=="lua" || suffix=="py")return Format::Plain;
    if(suffix=="json" || suffix=="jsonc")return Format::Json;
    if(suffix=="txt" || suffix=="xml")return Format::Plain;
    return Format::Unsupported;
}
bool isEditable(Format kind) noexcept {
    return kind==Format::JavaScript || kind==Format::TypeScript ||
           kind==Format::Json || kind==Format::Plain ||
           kind==Format::VertexShader || kind==Format::FragmentShader;
}
QPlainTextEdit* createSourceEditor(QWidget* parent,Format format,bool readOnly) {
    return makeCodeEditor(parent,format,readOnly);
}
void open(QWidget* parent,QString const& absolutePath,bool sharedAsset) {
    Format const format=formatFor(absolutePath);
    if(format==Format::Unsupported)return;
    if(!QFileInfo(absolutePath).isFile()) {
        QMessageBox::warning(parent,"Text Asset","The selected source file is unavailable.");
        return;
    }
    TextAssetDialog dialog(parent,absolutePath,format,sharedAsset);
    dialog.exec();
}
QWidget* createEmbedded(QWidget* parent,QString const& absolutePath,bool sharedAsset,
                        std::function<void(QString const&,bool)> onSaved,
                        std::function<void(QString const&,QByteArray const&)> onCompiled) {
    if(formatFor(absolutePath)==Format::Unsupported ||
       !QFileInfo(absolutePath).isFile())return nullptr;
    auto* editor=new TextAssetDialog(parent,absolutePath,formatFor(absolutePath),
                                     sharedAsset);
    editor->setEmbedded(std::move(onSaved),std::move(onCompiled));
    return editor;
}
bool confirmClose(QWidget* embedded) {
    auto* editor=dynamic_cast<TextAssetDialog*>(embedded);
    return editor && editor->canClose();
}
bool modified(QWidget* embedded) {
    auto* editor=dynamic_cast<TextAssetDialog*>(embedded);
    return editor && editor->dirty();
}
QString filePath(QWidget* embedded) {
    auto* editor=dynamic_cast<TextAssetDialog*>(embedded);
    return editor?editor->path():QString{};
}
} // namespace northstarcpp::editor::text_assets
