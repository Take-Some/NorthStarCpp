// Integration QA: document routing, Markdown rendering and safe code saves.
#include "qt_content_browser.hpp"
#include "qt_text_asset_editor.hpp"
#include "qt_text_asset_workspace.hpp"
#include <QCompleter>
#include <QTabWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QAbstractItemModel>
#include <northstarcpp/editor_asset_catalog.hpp>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QListView>
#include <QDir>
#include <QTreeWidget>
#include <QThread>
#include <QIcon>
#include <QTextDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimer>
#include <iostream>

namespace {
bool write(QString const& path,QByteArray const& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
}
QByteArray read(QString const& path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return {};
    return file.readAll();
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    using namespace northstarcpp::editor;
    int failures=0;
    auto require=[&](bool value,char const* message) {
        if(!value) {std::cerr<<"FAIL: "<<message<<"\n";++failures;}
    };
    QTemporaryDir dir;
    require(dir.isValid(),"temporary directory");
    if(!dir.isValid())return 2;
    auto const md=dir.filePath("README.md");
    auto const js=dir.filePath("game.js");
    auto const vert=dir.filePath("surface.vert");
    auto const frag=dir.filePath("surface.frag");
    require(write(md,"# NorthStar\n\n**Markdown preview**\n"),"write MD");
    require(write(js,QByteArray::fromHex("efbbbf")+"const value = 1;\r\n"),"write JS");
    require(write(vert,"#version 450\nvoid main() {}\n"),"write vert");
    require(write(frag,"#version 450\nvoid main() {}\n"),"write frag");
    require(text_assets::formatFor(md)==text_assets::Format::Markdown,"markdown routing");
    require(text_assets::formatFor(js)==text_assets::Format::JavaScript,"javascript routing");
    require(text_assets::formatFor(vert)==text_assets::Format::VertexShader,"vertex shader routing");
    require(text_assets::formatFor(frag)==text_assets::Format::FragmentShader,"fragment shader routing");
    require(text_assets::formatFor("model.ydd")==text_assets::Format::Unsupported,
            "unknown file not intercepted");
    require(kind_from_extension(std::filesystem::path("surface.vert"))==AssetKind::shader,
            "vertex shader catalog classification");
    require(kind_from_extension(std::filesystem::path("surface.frag"))==AssetKind::shader,
            "fragment shader catalog classification");

    { // Verify the exact file art attached to live Asset Browser cards.
        QtContentBrowser browser;
        for(auto const& name:{"fileMd.png","fileJs.png","vertShader.png","fragShader.png"})
            require(QFile(QString(":/northstar/icons/assetBrowser/")+name).exists(),
                    "icon missing from Qt resource collection");
        browser.setContentRoot(dir.path());
        QElapsedTimer elapsed;elapsed.start();
        while(elapsed.elapsed()<5000 && browser.visibleCount()<4) {
            QCoreApplication::processEvents(QEventLoop::AllEvents,40);
            QThread::msleep(5);
        }
        require(browser.visibleCount()==4,"four files indexed and visible");
        auto* grid=browser.findChild<QListView*>("assetGrid");
        require(grid!=nullptr,"browser asset grid");
        if(grid)for(int row=0;row<grid->model()->rowCount();++row) {
            QModelIndex index=grid->model()->index(row,0);
            QString const extension=QFileInfo(index.data(Qt::DisplayRole).toString()).suffix().toLower();
            QString art;
            if(extension=="md")art="fileMd.png";
            else if(extension=="js")art="fileJs.png";
            else if(extension=="vert")art="vertShader.png";
            else if(extension=="frag")art="fragShader.png";
            if(art.isEmpty())continue;
            QIcon const expected(QString(":/northstar/icons/assetBrowser/")+art);
            QIcon const actual=qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            require(!actual.isNull() && actual.pixmap(64,64).toImage()==
                    expected.pixmap(64,64).toImage(),"asset card uses correct icon");
        }
    }

    { // Project scripts must appear even though the scene mounts assets/.
        require(QDir().mkpath(dir.filePath("assets")),"assets fixture");
        require(QDir().mkpath(dir.filePath("scripts/player")),"script fixture");
        QString const script=dir.filePath("scripts/main.js");
        QString const nested=dir.filePath("scripts/player/controller.ts");
        require(write(script,"const game = 1;\n"),"main script source");
        require(write(nested,"export const speed = 4;\n"),"nested script source");
        QString const scene=dir.filePath("menu.nscn");
        require(write(scene,"scene"),"project scene path");
        QtContentBrowser browser;
        browser.setEditorDockMode(true);
        browser.setProjectScene(scene);
        browser.setSearchText("main.js");
        QElapsedTimer wait;wait.start();
        while(wait.elapsed()<6500 && browser.visibleCount()!=1) {
            QCoreApplication::processEvents(QEventLoop::AllEvents,40);
            QThread::msleep(10);
        }
        require(browser.visibleCount()==1,"project main.js indexed");
        auto* tree=browser.findChild<QTreeWidget*>();
        require(tree && !tree->findItems("scripts",
                Qt::MatchExactly|Qt::MatchRecursive,0).isEmpty(),
                "script folder visible in project hierarchy");
        require(tree && !tree->findItems("player",
                Qt::MatchExactly|Qt::MatchRecursive,0).isEmpty(),
                "nested script directory visible");
        browser.setSearchText("");
        browser.browseTo("scripts");
        auto* grid=browser.findChild<QListView*>("assetGrid");
        QModelIndex entry;
        if(grid)for(int row=0;row<grid->model()->rowCount();++row) {
            auto index=grid->model()->index(row,0);
            if(index.data(Qt::DisplayRole).toString()=="main.js")
                entry=index;
        }
        require(entry.isValid(),"main.js visible in scripts folder");
        QString activated;
        bool shared=true;
        QObject::connect(&browser,&QtContentBrowser::textEditorRequested,
            &browser,[&](QString const& filename,bool readonly) {
                activated=filename;shared=readonly;
            });
        if(grid && entry.isValid())
            QMetaObject::invokeMethod(grid,"doubleClicked",Qt::DirectConnection,
                                      Q_ARG(QModelIndex,entry));
        require(QDir::cleanPath(activated)==QDir::cleanPath(script) && !shared,
                "double click opens editable project script, not assets path");
        require(text_assets::formatFor(nested)==text_assets::Format::TypeScript,
                "nested .ts opens in TypeScript editor");
    }

    auto exercise=[&](QString const& filename,bool shared,
                      auto verify) {
        bool executed=false;
        QTimer::singleShot(30,&app,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            require(dialog!=nullptr,"modal opened");
            if(!dialog)return;
            executed=true;
            auto* source=dialog->findChild<QPlainTextEdit*>("northstarTextAssetSource");
            require(source!=nullptr,"text source created");
            if(source)verify(dialog,source);
            dialog->reject();
        });
        text_assets::open(nullptr,filename,shared);
        require(executed,"modal callback executed");
    };
    exercise(md,false,[&](QDialog* dialog,QPlainTextEdit* source) {
        auto* preview=dialog->findChild<QTextBrowser*>("northstarMarkdownPreview");
        require(preview!=nullptr,"Markdown preview widget");
        require(source->isReadOnly(),"Markdown source is read-only");
        require(preview && preview->toPlainText().contains("Markdown preview"),
                "Markdown content rendered");
        require(!dialog->findChild<QPushButton*>("textAssetSave"),
                "Markdown save control absent");
    });
    exercise(js,false,[&](QDialog* dialog,QPlainTextEdit* source) {
        auto* save=dialog->findChild<QPushButton*>("textAssetSave");
        require(!source->isReadOnly(),"project JavaScript is editable");
        require(save!=nullptr,"JavaScript save control");
        source->selectAll();
        source->insertPlainText("const value = 42;\n");
        require(source->document()->isModified(),"dirty state set");
        if(save)save->click();
        require(!source->document()->isModified(),"dirty state reset after save");
        require(read(js)==QByteArray::fromHex("efbbbf")+"const value = 42;\r\n",
                "atomic write retained UTF-8 BOM and CRLF");
    });
    exercise(vert,true,[&](QDialog* dialog,QPlainTextEdit* source) {
        require(source->isReadOnly(),"engine shader is read-only");
        require(!dialog->findChild<QPushButton*>("textAssetSave"),
                "engine shader Save control absent");
    });
    exercise(frag,false,[&](QDialog* dialog,QPlainTextEdit* source) {
        require(!source->isReadOnly(),"project fragment shader editable");
        require(dialog->findChild<QPushButton*>("textAssetSave")!=nullptr,
                "fragment shader Save control");
    });
    // Native dock regression: tabs, de-duplication, IntelliSense and search.
    {
        QtTextAssetWorkspace workspace;
        require(workspace.openFile(md,false),"open Markdown tab");
        require(workspace.openFile(js,false),"open JS tab");
        require(workspace.openFile(js,false),"reuse existing JS tab");
        require(workspace.documentCount()==2,"deduplicated document tabs");
        auto* tabs=workspace.tabs();
        require(tabs && tabs->objectName()=="textDocumentTabs","tab workspace object");
        auto* active=tabs?tabs->currentWidget():nullptr;
        auto* editor=active?active->findChild<QPlainTextEdit*>("northstarTextAssetSource"):nullptr;
        require(editor!=nullptr,"embedded document source");
        if(editor) {
            auto* completer=editor->findChild<QCompleter*>();
            require(completer!=nullptr,"JS IntelliSense installed");
            bool apiAvailable=false;
            if(completer) {
                auto* model=completer->model();
                for(int row=0;row<model->rowCount();++row)
                    if(model->index(row,0).data().toString()=="Camera.setMode")
                        {apiAvailable=true;break;}
            }
            require(apiAvailable,"IntelliSense indexes NorthStar d.ts");
            if(completer) {
                bool render=false,documentType=false;
                auto* model=completer->model();
                for(int row=0;row<model->rowCount();++row) {
                    QString const name=model->index(row,0).data().toString();
                    render|=(name=="UIRoot.render");
                    documentType|=(name=="UiDeclaration.children");
                }
                require(render,"UIRoot.render indexed from embedded TypeScript");
                require(documentType,"UiDeclaration fields indexed from d.ts");
                // Infer the type of a variable from its UI initializer.
                editor->setPlainText(
                    "import { UI } from \"@northstar/engine\";\n"
                    "const menu = UI.createRoot(\"menu\");\nmenu.r");
                editor->moveCursor(QTextCursor::End);
                QKeyEvent typed(QEvent::KeyPress,Qt::Key_E,
                                Qt::NoModifier,QStringLiteral("e"));
                QCoreApplication::sendEvent(editor,&typed);
                require(completer->completionPrefix()=="UIRoot.re",
                        "UI root alias offers render completion");
                // Semantic TypeScript completion is async and must not stall
                // the Qt thread. Wait for its typed, member-only result.
                QElapsedTimer tsWait;tsWait.start();
                bool semantic=false;
                while(tsWait.elapsed()<6500) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                    if(completer->completionPrefix()=="re") {
                        auto* matches=completer->model();
                        for(int row=0;row<matches->rowCount();++row)
                            if(matches->index(row,0).data().toString()=="render")
                                semantic=true;
                    }
                    if(semantic)break;
                    QThread::msleep(10);
                }
                require(semantic,
                        "real TypeScript checker completes menu.render");
            }
            // Real TypeScript diagnostics, inline errors and typo fix.
            auto* typeIssues=active->findChild<QListWidget*>("textAssetDiagnostics");
            auto* quickFix=active->findChild<QPushButton*>("textAssetQuickFix");
            require(typeIssues && quickFix,"TypeScript diagnostics UI");
            editor->setPlainText(
                "import {UI} from \"@northstar/engine\";\n"
                "const menu=UI.createRoot(\"main\");\n"
                "menu.rnder({id:\"p\",type:\"panel\"});\n"
                "menu.setText(\"title\",45);\n");
            QElapsedTimer issueWait;issueWait.start();
            bool reported=false;
            while(issueWait.elapsed()<6500) {
                QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                if(typeIssues)for(int index=0;index<typeIssues->count();++index)
                    reported|=typeIssues->item(index)->text().contains("TS2551");
                if(reported)break;
                QThread::msleep(10);
            }
            require(reported,"TypeScript diagnoses unknown UI method");
            require(editor->extraSelections().size()>1,
                    "Inline TypeScript diagnostics underlined");
            auto typo=editor->document()->find("rnder");
            if(quickFix && !typo.isNull()) {
                typo.setPosition(typo.selectionStart()+2);
                editor->setTextCursor(typo);
                quickFix->click();
                QElapsedTimer fixWait;fixWait.start();
                while(fixWait.elapsed()<6500 &&
                      !editor->toPlainText().contains("menu.render(")) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents,40);
                    QThread::msleep(10);
                }
                require(editor->toPlainText().contains("menu.render("),
                        "Quick Fix replaces misspelled UI method");
            }
            QKeyEvent shortcut(QEvent::ShortcutOverride,Qt::Key_Space,
                               Qt::ControlModifier);
            QCoreApplication::sendEvent(editor,&shortcut);
            require(shortcut.isAccepted(),"Ctrl+Space reserved for editor completion");
            auto* find=active->findChild<QLineEdit*>("textAssetFind");
            auto* replacement=active->findChild<QLineEdit*>("textAssetReplace");
            auto* all=active->findChild<QPushButton*>("textAssetReplaceAll");
            require(find && replacement && all,"search/replace controls");
            if(find && replacement && all) {
                editor->selectAll();
                editor->insertPlainText("const value = 1;\nconst value = 1;\n");
                find->setText("value");
                replacement->setText("score");
                all->click();
                require(editor->toPlainText().count("score")==2,
                        "Replace All changed both occurrences");
                require(tabs->tabText(tabs->currentIndex()).startsWith("*"),
                        "dirty tab marker");
                auto* save=active->findChild<QPushButton*>("textAssetSave");
                require(save!=nullptr,"embedded save action");
                if(save)save->click();
                require(read(js).contains("const score = 1;"),
                        "embedded save writes disk");
                require(!text_assets::modified(active),"dirty cleared on save");
                require(workspace.confirmCloseAll(),"saved tabs close without warning");
            }
        }
    }
    {
        QtTextAssetWorkspace shaderWorkspace;
        int validated=0;
        QObject::connect(&shaderWorkspace,&QtTextAssetWorkspace::assetSaved,
            &shaderWorkspace,[&](QString const& path,bool shader) {
                if(shader && path==QFileInfo(frag).canonicalFilePath())++validated;
            });
        require(shaderWorkspace.openFile(frag,false),"GLSL tab open");
        auto* pane=shaderWorkspace.tabs()->currentWidget();
        auto* source=pane->findChild<QPlainTextEdit*>("northstarTextAssetSource");
        auto* save=pane->findChild<QPushButton*>("textAssetSave");
        auto* diagnostics=pane->findChild<QListWidget*>("textAssetDiagnostics");
        require(source && save && diagnostics,"GLSL controls");
        if(source && save && diagnostics) {
            auto pump=[&](int duration,std::function<bool()> complete) {
                QElapsedTimer timer;timer.start();
                while(timer.elapsed()<duration && !complete()) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents,40);
                    QThread::msleep(5);
                }
            };
            source->selectAll();
            source->insertPlainText("#version 450\nlayout(location=0) out vec4 outColor;\n"
                                    "void main(){outColor=vec4(1.0);}\n");
            save->click();
            pump(7000,[&]{return validated==1;});
            require(validated==1,"real glslc validates saved fragment");
            source->selectAll();
            source->insertPlainText("#version 450\nvoid main( { INVALID }\n");
            save->click();
            pump(7000,[&] {
                for(int i=0;i<diagnostics->count();++i)
                    if(diagnostics->item(i)->text().contains("error",Qt::CaseInsensitive))
                        return true;
                return false;
            });
            require(validated==1,"invalid GLSL never requests hot reload");
            bool hasLine=false;
            for(int i=0;i<diagnostics->count();++i)
                if(diagnostics->item(i)->data(Qt::UserRole).toInt()>0)
                    hasLine=true;
            require(hasLine,"glslc error has navigable line");
        }
    }
    // GLSL validation QA will follow.
    // Native dock smoke follows.
    std::cout<<"NorthStar Qt text asset smoke: "<<(failures?"FAILED":"PASS")
             <<" ("<<failures<<" failures)\n";
    return failures?1:0;
}
