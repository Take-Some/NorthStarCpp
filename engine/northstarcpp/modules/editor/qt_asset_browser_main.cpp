// NorthStar native Qt 6 Content Browser host.
// QtContentBrowser is owned by Qt Editor; this executable is a QA/compatibility launcher.
#include "qt_content_browser.hpp"
#include "qt_nsp_icons.hpp"
#include "../shared/crash_handler.hpp"
#include "qt_nac_icons_smoke.hpp"
#include "qt_git_status_smoke.hpp"
#include "qt_content_browser_layout_smoke.hpp"
#include "qt_collision_icons_smoke.hpp"
#include "qt_menu_theme.hpp"
#include "qt_asset_card_delegate.hpp"
#include "qt_prefab_preview.hpp"
#include "qt_editor_viewport.hpp"
#include <northstarcpp/scene.hpp>
#include <northstarcpp/nac.hpp>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListView>
#include <QTreeWidget>
#include <QAbstractItemModel>
#include <QMainWindow>
#include <QDockWidget>
#include <QProcess>
#include <QDialog>
#include <QMetaObject>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QSlider>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QIcon>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QStandardItemModel>
#include <array>
#include <iostream>
#include <memory>

int main(int argc,char** argv) {
    northstarcpp::diagnostics::install_crash_handler();
    QApplication app(argc,argv);
    northstarcpp::editor::ensure_northstar_qt_icons();
    app.setApplicationName("NorthStar Content Browser");
    app.setOrganizationName("Kayla's Systems");
    northstarcpp::editor::installMenuTheme(
        app,northstarcpp::editor::MenuTheme::Dark);
    QCommandLineParser parser;
    parser.setApplicationDescription("Native, cross-platform NorthStar asset browser");
    parser.addHelpOption();
    QCommandLineOption scene({"p","project"},
        "Project scene file whose sibling assets folder will be browsed.","scene");
    QCommandLineOption content({"c","content"},
        "Explicit Content directory to browse.","directory");
    QCommandLineOption smoke("smoke","Run Qt asset-browser integration smoke and exit.");
    QCommandLineOption assetCardsSmoke("asset-cards-smoke",
        "Check complete UE-style asset cards, color bands, metadata and previews.");
    QCommandLineOption folderIconsSmoke("folder-icons-smoke",
        "Verify physical and virtual NEF8 folder artwork in grid, tree and details.");
    QCommandLineOption categoryIconsSmoke("category-icons-smoke",
        "Verify Other and Material Function PNG icons in grid, list and details.");
    QCommandLineOption gitBadgeSmoke("git-badge-smoke",
        "Verify the Git status badge on a real untracked project asset.");
    QCommandLineOption layoutStabilitySmoke("layout-stability-smoke",
        "Check selection and metadata do not change dock height.");
    QCommandLineOption nacIconsSmoke("nac-icons-smoke",
        "Verify NAC audio container and individual sound file PNG artwork.");
    QCommandLineOption collisionIconsSmoke("collision-icons-smoke",
        "Verify collision directory and .ybn file PNG artwork.");
    QCommandLineOption embeddedIconsSmoke("embedded-icons-smoke",
        "Verify native YMT/YCD virtual children use skeleton/animation file icons.");
    QCommandLineOption previewModalSmoke("preview-modal-smoke",
        "Also exercise real double-click modal preview of NEF8 texture/model.");
    QCommandLineOption prefabCardsSmoke("prefab-cards-smoke",
        "Verify project prefab card, original 3D preview and modal inspection.");
    QCommandLineOption multiPrefabSmoke("prefab-composition-smoke",
        "Verify native two-object Prefab composition, transforms and lighting.");
    QCommandLineOption gpuPrefab("gpu-prefab",
        "Prefab path relative to --content project root for GPU preview QA.",
        "reference","prefabs/mahir_mt28.nprefab");
    QCommandLineOption gpuCapture("gpu-capture",
        "Save actual GPU viewport readback in MT28 preview smoke.","filename");
    QCommandLineOption mt28GpuSmoke("mt28-gpu-preview-smoke",
        "Benchmark native engine.render GPU preview with the full MT28 Scene V3.");
    QCommandLineOption mini14PrefabSmoke("mini14-prefab-smoke",
        "Check shared Mini-14 prefab native 3D modal, not its schematic fallback.");
    QCommandLineOption screenshot("screenshot",
        "Save a screenshot after the asset catalog finishes indexing.","filename");
    parser.addOption(scene);
    parser.addOption(content);
    parser.addOption(smoke);
    parser.addOption(assetCardsSmoke);
    parser.addOption(folderIconsSmoke);
    parser.addOption(categoryIconsSmoke);
    parser.addOption(nacIconsSmoke);
    parser.addOption(layoutStabilitySmoke);
    parser.addOption(gitBadgeSmoke);
    parser.addOption(collisionIconsSmoke);
    parser.addOption(embeddedIconsSmoke);
    parser.addOption(previewModalSmoke);
    parser.addOption(prefabCardsSmoke);
    parser.addOption(multiPrefabSmoke);
    parser.addOption(mini14PrefabSmoke);
    parser.addOption(mt28GpuSmoke);
    parser.addOption(gpuCapture);
    parser.addOption(gpuPrefab);
    parser.addOption(screenshot);
    parser.process(app);
    // Compatibility launcher only. Normal authoring uses the engine-owned
    // Content Browser dock inside northstarcpp_editor_qt.exe. Keep this
    // standalone process solely for explicit thumbnail/preview QA modes.
    bool const qa=parser.isSet(gitBadgeSmoke) ||
        parser.isSet(layoutStabilitySmoke) || parser.isSet(smoke) || parser.isSet(assetCardsSmoke) ||
        parser.isSet(folderIconsSmoke) || parser.isSet(categoryIconsSmoke) ||
        parser.isSet(embeddedIconsSmoke) || parser.isSet(nacIconsSmoke) ||
        parser.isSet(collisionIconsSmoke) ||
        parser.isSet(previewModalSmoke) ||
        parser.isSet(prefabCardsSmoke) || parser.isSet(multiPrefabSmoke) ||
        parser.isSet(mini14PrefabSmoke) || parser.isSet(mt28GpuSmoke) || parser.isSet(screenshot);
    if(!qa) {
        auto const executable=QDir(QCoreApplication::applicationDirPath())
            .filePath("northstarcpp_editor_qt.exe");
        if(!QFileInfo::exists(executable)) {
            std::cerr<<"NorthStar Qt Editor not installed: "
                     <<executable.toStdString()<<std::endl;
            return 2;
        }
        QStringList arguments{"--focus-content-browser"};
        if(parser.isSet(scene)) {
            arguments << "--scene" << parser.value(scene);
        } else if(parser.isSet(content)) {
            // --content was an old standalone-only argument. Derive a
            // project scene when possible; never browse an arbitrary root
            // outside an Editor project as a substitute.
            QDir directory(parser.value(content));
            if(directory.exists() && directory.dirName().compare(
                    "assets",Qt::CaseInsensitive)==0 && directory.cdUp()) {
                auto const scenes=directory.entryInfoList(
                    {"*.nscn","*.nscene"},QDir::Files,QDir::Name);
                if(!scenes.isEmpty())
                    arguments << "--scene" << scenes.front().absoluteFilePath();
            }
        }
        QProcess editor;
        editor.setProgram(executable);
        editor.setArguments(arguments);
        editor.setWorkingDirectory(QFileInfo(executable).absolutePath());
        if(!editor.startDetached()) {
            std::cerr<<"Could not start NorthStar Qt Editor: "
                     <<editor.errorString().toStdString()<<std::endl;
            return 3;
        }
        return 0;
    }
    auto browser=std::make_unique<northstarcpp::editor::QtContentBrowser>();
    std::unique_ptr<QTemporaryDir> sandbox;
    if(parser.isSet(gitBadgeSmoke) && !parser.isSet(scene) &&
       !parser.isSet(content)) {
        sandbox=std::make_unique<QTemporaryDir>();
        if(!northstarcpp::editor::git_browser_qa::prepare(*sandbox,*browser))
            return 4;
    } else if(parser.isSet(layoutStabilitySmoke) && !parser.isSet(scene) &&
       !parser.isSet(content)) {
        sandbox=std::make_unique<QTemporaryDir>();
        if(!northstarcpp::editor::browser_layout_qa::prepare(*sandbox,*browser))
            return 4;
    } else if(parser.isSet(collisionIconsSmoke) && !parser.isSet(scene) &&
       !parser.isSet(content)) {
        sandbox=std::make_unique<QTemporaryDir>();
        if(!northstarcpp::editor::collision_browser_qa::prepare(
               *sandbox,*browser))return 4;
    } else if(parser.isSet(nacIconsSmoke) && !parser.isSet(scene) &&
       !parser.isSet(content)) {
        sandbox=std::make_unique<QTemporaryDir>();
        if(!northstarcpp::editor::nac_browser_qa::prepare(*sandbox,*browser))return 4;
    } else if(parser.isSet(categoryIconsSmoke) && !parser.isSet(scene) &&
       !parser.isSet(content)) {
        sandbox=std::make_unique<QTemporaryDir>();
        if(!sandbox->isValid())return 3;
        QFile materialFunction(sandbox->path()+"/sample.material-function.json");
        QFile other(sandbox->path()+"/sample.unknown");
        if(!materialFunction.open(QIODevice::WriteOnly) ||
           !other.open(QIODevice::WriteOnly))return 4;
        materialFunction.write("{\"schema\":\"northstar.material-function.v1\","
                               "\"inputs\":[],\"outputs\":[],\"nodes\":[]}");
        other.write("ordinary file");
        materialFunction.close();
        other.close();
        browser->setContentRoot(sandbox->path());
    } else if(parser.isSet(smoke) && !parser.isSet(scene) &&
              !parser.isSet(content)) {
        sandbox=std::make_unique<QTemporaryDir>();
        if(!sandbox->isValid())return 3;
        QDir(sandbox->path()).mkpath("assets/Meshes");
        QDir(sandbox->path()).mkpath("assets/Textures");
        QFile a(sandbox->path()+"/assets/Meshes/npc.ydd");
        QFile b(sandbox->path()+"/assets/Textures/albedo.ytd");
        QFile c(sandbox->path()+"/assets/gameplay.ysc");
        if(!a.open(QIODevice::WriteOnly) || !b.open(QIODevice::WriteOnly) ||
           !c.open(QIODevice::WriteOnly)) return 4;
        a.write("mesh");b.write("texture");c.write("script");
        a.close();b.close();c.close();
        browser->setContentRoot(sandbox->path()+"/assets");
    } else if(parser.isSet(content))
        browser->setContentRoot(parser.value(content));
    else if(parser.isSet(scene))
        browser->setProjectScene(parser.value(scene));

    QMainWindow window;
    window.setObjectName("northstarAssetBrowserWindow");
    window.setWindowTitle("NorthStar Engine Р В Р’В Р вЂ™Р’В Р В Р’В Р Р†Р вЂљР’В Р В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р РЋРІвЂћСћР В Р’В Р В РІР‚В Р В Р’В Р Р†Р вЂљРЎв„ўР В Р Р‹Р РЋРЎв„ў Content Browser");
    window.resize(1460,880);
    window.setMinimumSize(950,550);
    // The native browser is a QWidget; the Qt editor can place it into any
    // QDockWidget or tabbed document area without changing its implementation.
    window.setCentralWidget(browser.get());
    browser.release(); // ownership moves to QMainWindow
    auto* view=static_cast<northstarcpp::editor::QtContentBrowser*>(window.centralWidget());
    QObject::connect(view,&northstarcpp::editor::QtContentBrowser::assetSelected,
        &window,[&window](QString const& reference) {
        window.statusBar()->showMessage(reference.isEmpty()?
            "Content ready":"Selected: Content / "+reference);
    });
    QObject::connect(view,&northstarcpp::editor::QtContentBrowser::assetActivated,
        &window,[&window](QString const& reference) {
        window.statusBar()->showMessage("Asset: Content / "+reference,8000);
    });
    auto* fileMenu=window.menuBar()->addMenu("&Content");
    fileMenu->addAction("&Refresh",QKeySequence(Qt::Key_F5),
        view,&northstarcpp::editor::QtContentBrowser::refresh);
    fileMenu->addAction("Scene Sky...",&window,[view] {view->editSceneSky();});
    fileMenu->addSeparator();
    fileMenu->addAction("&Close",QKeySequence::Close,&window,&QMainWindow::close);
    auto* helpMenu=window.menuBar()->addMenu("&Help");
    helpMenu->addAction("About Content Browser",&window,[&window] {
        QMessageBox::about(&window,"NorthStar Content Browser",
            "Native C++/Qt6 asset browser\n"
            "Project-scoped index, editable content, and asynchronous previews.\n"
            "Kayla's Systems Р В Р’В Р вЂ™Р’В Р В Р’В Р Р†Р вЂљР’В Р В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р РЋРІвЂћСћР В Р’В Р В РІР‚В Р В Р’В Р Р†Р вЂљРЎв„ўР В Р Р‹Р РЋРЎв„ў 2026");
    });
    window.statusBar()->showMessage("Native Content Browser  |  F5 Refresh  |  "
        "Double-click to preview assets  |  Right-click to insert meshes");
    QDockWidget* layoutDock=nullptr;
    if(parser.isSet(layoutStabilitySmoke)) {
        QWidget* embedded=window.takeCentralWidget();
        layoutDock=new QDockWidget("Content Browser",&window);
        layoutDock->setObjectName("contentDock");
        layoutDock->setWidget(embedded);
        auto* center=new QWidget(&window);
        center->setMinimumSize(640,240);
        window.setCentralWidget(center);
        window.addDockWidget(Qt::BottomDockWidgetArea,layoutDock);
    }
    window.show();
    if(parser.isSet(mt28GpuSmoke)) {
#ifdef _WIN32
        try {
            namespace fs=std::filesystem;
            auto const project=fs::u8path(
                parser.value(content).toUtf8().toStdString());
            auto const prefabRef=parser.value(gpuPrefab).toUtf8().toStdString();
            auto const contentRoot=fs::is_directory(project/"assets")?
                project/"assets":project;
            auto mt28=northstarcpp::editor::prefab_preview::load(
                contentRoot,prefabRef);
            northstarcpp::editor::QtEditorViewport gpuViewport;
            gpuViewport.resize(960,640);
            gpuViewport.setAssetSources(contentRoot);
            gpuViewport.setScene(&mt28);
            gpuViewport.setAssetPreviewMode(true);
            gpuViewport.setShowGrid(false);
            gpuViewport.setOrbitMode(true,{0.f,1.6f,0.f},19.f);
            gpuViewport.setOrbitAngles(.62f,-.27f);
            gpuViewport.show();
            QCoreApplication::processEvents(QEventLoop::AllEvents,40);
            gpuViewport.setDrawMode(
                northstarcpp::editor::QtEditorViewport::DrawMode::EngineGpu);
            // Large vehicles stream full-res models asynchronously.
            // Do not report a fast empty GPU frame as a performance result.
            bool success=gpuViewport.gpuActive();
            QElapsedTimer warmup;warmup.start();
            QString loadedStats;
            int nativeMeshes=0;
            int stableFrames=0,previousMeshes=-1;
            while(success && warmup.elapsed()<15000 &&
                  !(nativeMeshes>=int(std::min<std::size_t>(40,mt28.objects.size())) && stableFrames>=5)) {
                QCoreApplication::processEvents(QEventLoop::AllEvents,20);
                QThread::msleep(25);
                success=gpuViewport.renderGpuNow();
                loadedStats=gpuViewport.gpuAssetSummary();
                nativeMeshes=loadedStats.section("Native meshes: ",1,1)
                    .section(" |",0,0).toInt();
                stableFrames=nativeMeshes>0 && nativeMeshes==previousMeshes?
                    stableFrames+1:0;
                previousMeshes=nativeMeshes;
            }
            std::vector<double> times;
            for(int i=0;i<12 && success && nativeMeshes>0;++i) {
                QCoreApplication::processEvents(QEventLoop::AllEvents,15);
                gpuViewport.setOrbitAngles(.62f+float(i)*.075f,-.27f);
                QElapsedTimer frame;frame.start();
                success=gpuViewport.renderGpuNow();
                times.push_back(double(frame.nsecsElapsed())/1.e6);
            }
            if(parser.isSet(gpuCapture) && success && nativeMeshes>0) {
                const auto pixels=gpuViewport.captureRenderedFrame();
                if(pixels.isNull()||!pixels.save(parser.value(gpuCapture),"PNG")){
                    std::cerr<<"MT28_GPU_PREVIEW_ERROR GPU capture failed\n";
                    return 24;
                }
            }
            auto const summary=gpuViewport.gpuAssetSummary();
            std::cout<<"MT28_GPU_PREVIEW active="<<gpuViewport.gpuActive()
                     <<" prefab="<<prefabRef
                     <<" actors="<<mt28.objects.size()
                     <<" warmup_ms="<<warmup.elapsed()
                     <<" native_meshes_after_warmup="<<nativeMeshes
                     <<" summary="<<summary.toStdString()<<"\n";
            std::cout<<"MT28_GPU_FRAME_CPU_MS";
            for(auto sample:times)std::cout<<" "<<sample;
            std::cout<<"\n";
            return success && !mt28.objects.empty() &&
                nativeMeshes>0?0:21;
        }catch(std::exception const& error) {
            std::cerr<<"MT28_GPU_PREVIEW_ERROR "<<error.what()<<"\n";
            return 22;
        }
#else
        std::cout<<"MT28_GPU_PREVIEW unsupported platform\n";
        return 23;
#endif
    }

    if(parser.isSet(gitBadgeSmoke) || parser.isSet(layoutStabilitySmoke) ||
       parser.isSet(smoke) ||
       parser.isSet(folderIconsSmoke) ||
       parser.isSet(categoryIconsSmoke) || parser.isSet(embeddedIconsSmoke) ||
       parser.isSet(nacIconsSmoke) || parser.isSet(collisionIconsSmoke) ||
       parser.isSet(assetCardsSmoke) || parser.isSet(prefabCardsSmoke) ||
       parser.isSet(multiPrefabSmoke) || parser.isSet(mini14PrefabSmoke) ||
       parser.isSet(screenshot)) {
        QElapsedTimer elapsed;
        elapsed.start();
        while(elapsed.elapsed()<10000 &&
              view->recordCount()==0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents,50);
            QThread::msleep(10);
        }
        if(parser.isSet(mini14PrefabSmoke)) {
            namespace role=northstarcpp::editor::asset_card_roles;
            view->browseTo("prefabs/weapon");
            auto* grid=view->findChild<QListView*>("assetGrid");
            auto* model=grid?grid->model():nullptr;
            QModelIndex selected;
            if(model)for(int i=0;i<model->rowCount();++i) {
                auto candidate=model->index(i,0);
                if(candidate.data(role::title).toString()=="rifle_mini14.nprefab") {
                    selected=candidate;
                    break;
                }
            }
            bool okay=selected.isValid();
            if(okay) {
                grid->setCurrentIndex(selected);
                bool rendered=false;
                QElapsedTimer timeout;timeout.start();
                QTimer poll;poll.setInterval(35);
                QObject::connect(&poll,&QTimer::timeout,&window,[&] {
                    auto* modal=qobject_cast<QDialog*>(
                        QApplication::activeModalWidget());
                    if(modal && modal->objectName()=="prefabPreviewDialog") {
                        auto* canvas=modal->findChild<QWidget*>(
                            "prefabPreviewCanvas");
                        auto* info=modal->findChild<QLabel*>(
                            "prefabPreviewDetails");
                        if(canvas && info &&
                           canvas->property("previewReady").toBool() &&
                           canvas->property("previewRendered").toBool() &&
                           canvas->property("prefabActorCount").toInt()==1 &&
                           canvas->property("previewMode").toString()==
                               "prefab-composition" &&
                           info->text().contains("Native models: 1") &&
                           info->text().contains("Missing models: 0")) {
                            rendered=!canvas->grab().isNull();
                            modal->accept();
                        }
                    }
                    if(timeout.elapsed()>20000 && modal)modal->reject();
                });
                poll.start();
                bool invoked=QMetaObject::invokeMethod(
                    grid,"doubleClicked",Qt::DirectConnection,
                    Q_ARG(QModelIndex,selected));
                poll.stop();
                okay=invoked && rendered;
            }
            std::cout<<"Mini-14 shared prefab native 3D Qt preview: "
                     <<(okay?"PASS":"FAIL")<<"\n";
            return okay?0:13;
        }
        if(parser.isSet(multiPrefabSmoke)) {
            namespace role=northstarcpp::editor::asset_card_roles;
            namespace fs=std::filesystem;
            auto const original=parser.value(content);
            QTemporaryDir fixture(QDir::currentPath()+
                "/northstar-prefab-composition-XXXXXX");
            bool okay=fixture.isValid() &&
                QDir().mkpath(fixture.path()+"/assets") &&
                QDir().mkpath(fixture.path()+"/prefabs");
            if(okay)try {
                auto scene=northstarcpp::editor::prefab_preview::load(
                    fs::u8path(original.toUtf8().toStdString()),
                    "prefabs/weapon/pistol.nprefab");
                auto child=scene.objects.front();
                child.node.id=scene.objects.front().node.id+1;
                child.node.parent=scene.objects.front().node.id;
                child.name="Second native pistol";
                child.position.x+=2.5f;
                child.node.rotation.y=40.f;
                child.node.scale.x=.85f;
                child.node.scale.z=1.15f;
                scene.objects.push_back(std::move(child));
                northstarcpp::validate_scene(scene);
                auto bytes=northstarcpp::serialize_scene_v3(scene);
                QFile file(fixture.path()+"/prefabs/pistol_double.nprefab");
                if(!file.open(QIODevice::WriteOnly) ||
                   file.write(bytes.data(),bytes.size())!=
                       static_cast<qint64>(bytes.size()))
                    okay=false;
                file.close();
            }catch(std::exception const&) {okay=false;}
            if(okay) {
                view->setContentRoot(fixture.path()+"/assets");
                // recordCount() may still return the previous project index
                // while QtConcurrent scans the new sandbox. Wait for the
                // actual new asset card instead, not a stale record count.
                view->setSearchText("pistol_double.nprefab");
                QElapsedTimer wait;wait.start();
                while(wait.elapsed()<12000 && view->visibleCount()!=1) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                    QThread::msleep(10);
                }
                view->setSearchText("");
                view->browseTo("prefabs/weapon");
                auto* grid=view->findChild<QListView*>("assetGrid");
                auto* model=grid?grid->model():nullptr;
                QModelIndex selected;
                if(model)for(int row=0;row<model->rowCount();++row) {
                    auto index=model->index(row,0);
                    if(index.data(role::title).toString()=="pistol_double.nprefab") {
                        selected=index;
                        break;
                    }
                }
                okay=selected.isValid() &&
                     selected.data(role::subtitle).toString() ==
                         "Prefab - 2 actors";
                if(okay) {
                    QElapsedTimer thumbnails;thumbnails.start();
                    while(thumbnails.elapsed()<18000 &&
                         view->cachedThumbnail("prefabs/pistol_double.nprefab")
                           .isNull()) {
                        QCoreApplication::processEvents(
                            QEventLoop::AllEvents,40);
                        QThread::msleep(15);
                    }
                    okay=!view->cachedThumbnail(
                        "prefabs/pistol_double.nprefab").isNull();
                }
                if(okay) {
                    grid->setCurrentIndex(selected);
                    bool seen=false,rendered=false,lighting=false;
                    QElapsedTimer timeout;timeout.start();
                    QTimer poll;poll.setInterval(35);
                    QObject::connect(&poll,&QTimer::timeout,&window,[&] {
                        auto* modal=qobject_cast<QDialog*>(
                            QApplication::activeModalWidget());
                        if(modal && modal->objectName()=="prefabPreviewDialog") {
                            seen=true;
                            auto* canvas=modal->findChild<QWidget*>(
                                "prefabPreviewCanvas");
                            auto* info=modal->findChild<QLabel*>(
                                "prefabPreviewDetails");
                            auto* slider=modal->findChild<QSlider*>(
                                "prefabPreviewKeyLight");
                            if(canvas && info && slider &&
                               canvas->property("previewReady").toBool() &&
                               canvas->property("previewRendered").toBool() &&
                               canvas->property("prefabActorCount").toInt()==2 &&
                               canvas->property("previewMode").toString()==
                                   "prefab-composition" &&
                               info->text().contains("Native models: 2") &&
                               info->text().contains("Missing models: 0")) {
                                rendered=true;
                                poll.stop(); // prevent reentrant timer callbacks
                                bool const gpuNative=
                                    canvas->property("previewBackend").toString()=="engine.render";
                                auto const beforeRevision=
                                    canvas->property("gpuRenderRevision").toULongLong();
                                QImage bright=canvas->grab().toImage();
                                slider->setValue(0);
                                // A new frame is now computed asynchronously;
                                // wait for its completion before comparing.
                                QElapsedTimer waitLight;waitLight.start();
                                while(waitLight.elapsed()<7000 &&
                                      !canvas->property("previewRendered").toBool()) {
                                    QCoreApplication::processEvents(
                                        QEventLoop::AllEvents,30);
                                    QThread::msleep(10);
                                }
                                QImage dim=canvas->grab().toImage();
                                if(gpuNative) {
                                    // Native child swapchain pixels are not
                                    // included in QWidget::grab(). Confirm
                                    // a fresh engine.render frame submission.
                                    lighting=canvas->property("previewRendered").toBool() &&
                                        canvas->property("gpuRenderRevision").toULongLong()>beforeRevision &&
                                        canvas->property("gpuAssetSummary").toString().contains("Native meshes:");
                                } else {
                                    lighting=canvas->property("previewRendered").toBool() &&
                                        !bright.isNull() && !dim.isNull() &&
                                        bright!=dim;
                                }

                                modal->accept();
                            }
                        }
                        if(timeout.elapsed()>20000 && modal)modal->reject();
                    });
                    poll.start();
                    bool invoked=QMetaObject::invokeMethod(grid,
                        "doubleClicked",Qt::DirectConnection,
                        Q_ARG(QModelIndex,selected));
                    poll.stop();
                    okay=invoked && seen && rendered && lighting;
                }
            }
            std::cout<<"Native multi-object Prefab 3D Qt preview: "
                     <<(okay?"PASS":"FAIL")<<"\n";
            return okay?0:11;
        }
        if(parser.isSet(prefabCardsSmoke)) {
            namespace role=northstarcpp::editor::asset_card_roles;
            view->browseTo("prefabs/weapon");
            auto* grid=view->findChild<QListView*>("assetGrid");
            auto* model=grid?grid->model():nullptr;
            QModelIndex index;
            if(model)for(int row=0;row<model->rowCount();++row) {
                auto candidate=model->index(row,0);
                if(candidate.data(role::title).toString()=="pistol.nprefab") {
                    index=candidate;
                    break;
                }
            }
            bool okay=index.isValid();
            if(okay) {
                okay=index.data(role::subtitle).toString()=="Prefab - 1 actor" &&
                     index.data(role::accent_color).value<QColor>()==
                         QColor("#55C29F");
            }
            if(okay) {
                QElapsedTimer wait;wait.start();
                while(wait.elapsed()<15000 &&
                      view->cachedThumbnail("prefabs/weapon/pistol.nprefab").isNull()) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                    QThread::msleep(15);
                }
                okay=!view->cachedThumbnail("prefabs/weapon/pistol.nprefab").isNull();
            }
            if(okay) {
                grid->setCurrentIndex(index);
                bool modalSeen=false,rendered=false;
                QElapsedTimer wait;wait.start();
                QTimer poll;
                poll.setInterval(35);
                QObject::connect(&poll,&QTimer::timeout,&window,[&] {
                    auto* modal=qobject_cast<QDialog*>(
                        QApplication::activeModalWidget());
                    if(modal && modal->objectName()=="prefabPreviewDialog") {
                        modalSeen=true;
                        auto* canvas=modal->findChild<QWidget*>("prefabPreviewCanvas");
                        auto* details=modal->findChild<QLabel*>("prefabPreviewDetails");
                        if(canvas && details &&
                           canvas->property("previewReady").toBool() &&
                           canvas->property("previewRendered").toBool() &&
                           details->text().contains("3D model: orbit") &&
                           details->text().contains("Pistol")) {
                            rendered=true;
                            modal->accept();
                        }
                    }
                    if(wait.elapsed()>15000 && modal)modal->reject();
                });
                poll.start();
                bool invoked=QMetaObject::invokeMethod(grid,"doubleClicked",
                    Qt::DirectConnection,Q_ARG(QModelIndex,index));
                poll.stop();
                okay=invoked && modalSeen && rendered;
            }
            std::cout<<"Prefab browser card and real original 3D preview: "
                     <<(okay?"PASS":"FAIL")<<"\n";
            return okay?0:7;
        }
        if(parser.isSet(assetCardsSmoke)) {
            namespace role=northstarcpp::editor::asset_card_roles;
            auto* grid=view->findChild<QListView*>("assetGrid");
            bool okay=grid && grid->model();
            auto* cards=okay?dynamic_cast<
                northstarcpp::editor::AssetCardDelegate*>(grid->itemDelegate()):nullptr;
            okay=okay && cards && cards->cellSize()==QSize(200,226);
            struct FolderCase {
                QString parent,category,meta,color;
                int count;
            };
            std::array<FolderCase,4> const tests{{
                {"textures/skydome.ytd","Texture","Texture Р В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р Р†РІР‚С›РЎС›Р В Р’В Р Р†Р вЂљРІвЂћСћР В РІР‚в„ўР вЂ™Р’В· ", "#4DBA87",5},
                {"models/skydome.ydd","Mesh","Mesh Р В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р Р†РІР‚С›РЎС›Р В Р’В Р Р†Р вЂљРІвЂћСћР В РІР‚в„ўР вЂ™Р’В· embedded","#58A7DB",1},
                {"materials/skydome.ymat","Material","Material Р В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р Р†РІР‚С›РЎС›Р В Р’В Р Р†Р вЂљРІвЂћСћР В РІР‚в„ўР вЂ™Р’В· embedded","#A66AD6",3},
                {"","Folder","Folder","#C99A52",3}
            }};
            int checked=0;
            for(auto const& tc:tests) {
                if(!okay)break;
                view->browseTo(tc.parent);
                QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                if(tc.parent.isEmpty())
                    okay=view->visibleCount()>=tc.count;
                else okay=view->visibleCount()==tc.count;
                if(!okay)break;
                int const target=tc.parent.isEmpty()?0:grid->model()->rowCount();
                int const checkCount=tc.parent.isEmpty()?1:target;
                for(int row=0;row<checkCount && okay;++row) {
                    auto ix=grid->model()->index(row,0);
                    auto label=ix.data(role::title).toString();
                    auto meta=ix.data(role::subtitle).toString();
                    auto accent=ix.data(role::accent_color).value<QColor>();
                    okay=!label.isEmpty() && meta.startsWith(tc.meta) &&
                         accent==QColor(tc.color);
                    if(!okay)break;
                    // Paint into an isolated cell to verify the colored band,
                    // card boundaries and isolated footer; this catches
                    // regression to QListView's clipped decoration renderer.
                    auto size=cards->cellSize();
                    QImage painted(size,QImage::Format_ARGB32);
                    painted.fill(QColor("#050b12"));
                    QStyleOptionViewItem option;
                    option.rect=QRect(QPoint(0,0),size);
                    option.state=QStyle::State_Enabled|QStyle::State_Selected;
                    QPainter painter(&painted);
                    cards->paint(&painter,option,ix);
                    painter.end();
                    int const stripeY=6+8+132-1+7+2;
                    okay=painted.pixelColor(size.width()/2,stripeY)==accent &&
                         painted.pixelColor(1,1)==QColor("#050b12") &&
                         painted.pixelColor(size.width()-2,size.height()-2)==
                             QColor("#050b12");
                    if(okay)++checked;
                }
            }
            // Asynchronous previews must populate the dedicated QImage role
            // without replacing the fallback QIcon and preserving category.
            if(okay) {
                view->browseTo("textures/skydome.ytd");
                QElapsedTimer elapsed;elapsed.start();
                while(elapsed.elapsed()<12000) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents,30);
                    bool all=true;
                    for(int i=0;i<grid->model()->rowCount();++i) {
                        auto ix=grid->model()->index(i,0);
                        all=all && !ix.data(role::preview_image).value<QImage>().isNull();
                    }
                    if(all)break;
                    QThread::msleep(10);
                }
                for(int i=0;i<grid->model()->rowCount();++i) {
                    auto ix=grid->model()->index(i,0);
                    okay=okay && !ix.data(role::preview_image).value<QImage>().isNull()
                         && ix.data(Qt::DecorationRole).canConvert<QIcon>();
                }
            }
            // Slider updates the full item height, avoiding the old clipped
            // "larger icon in a smaller cell" condition.
            if(okay) {
                auto* slider=view->findChild<QSlider*>();
                if(slider) {
                    slider->setValue(208);
                    QCoreApplication::processEvents();
                    okay=cards->cellSize().height()>226 &&
                         grid->gridSize()==cards->cellSize();
                    slider->setValue(144);
                } else okay=false;
            }
            if(okay) {
                for(auto* button:view->findChildren<QToolButton*>()) {
                    if(button->toolTip()=="Toggle grid/list view") {
                        button->click();
                        okay=grid->viewMode()==QListView::ListMode &&
                             grid->itemDelegate()!=cards;
                        button->click();
                        okay=okay && grid->viewMode()==QListView::IconMode &&
                             grid->itemDelegate()==cards;
                        break;
                    }
                }
            }
            std::cout<<"Qt asset card delegate: "<<(okay?"PASS":"FAIL")
                     <<" ("<<checked<<" painted category cards)\n";
            return okay?0:9;
        }
        if(parser.isSet(gitBadgeSmoke))
            return northstarcpp::editor::git_browser_qa::verify(*view)?0:17;
        if(parser.isSet(layoutStabilitySmoke))
            return northstarcpp::editor::browser_layout_qa::verify(
                window,*view,*layoutDock)?0:16;
        if(parser.isSet(collisionIconsSmoke))
            return northstarcpp::editor::collision_browser_qa::verify(*view)?0:15;
        if(parser.isSet(nacIconsSmoke))
            return northstarcpp::editor::nac_browser_qa::verify(*view)?0:13;
        if(parser.isSet(embeddedIconsSmoke)) {
            auto* grid=view->findChild<QListView*>("assetGrid");
            auto* details=view->findChild<QLabel*>("assetPreviewIcon");
            bool okay=grid && grid->model() && details;
            auto const sameIcon=[](QIcon const& rendered,QString const& path) {
                QIcon expected(path);
                return !expected.isNull() && !rendered.isNull() &&
                    expected.pixmap(128,128).toImage() ==
                    rendered.pixmap(128,128).toImage();
            };
            auto const artwork=[](char const* filename) {
                return QString(":/northstar/icons/assetBrowser/")+filename;
            };
            struct Case {
                QString container;
                QString folderArt;
                QString fileArt;
            };
            std::array<Case,2> cases{{
                {"models/weapon/pistol/common-pistol.ymt",
                 artwork("skeletalFolder.png"),artwork("skelFile.png")},
                {"animations/weapon/pistol/military-pistol.ycd",
                 artwork("animFolder.png"),artwork("animFile.png")}
            }};
            int verified=0;
            for(auto const& tc:cases) {
                if(!okay)break;
                auto parent=tc.container.section('/',0,-2);
                auto filename=tc.container.section('/',-1);
                view->browseTo(parent);
                QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                QModelIndex folder;
                for(int row=0;row<grid->model()->rowCount();++row) {
                    auto ix=grid->model()->index(row,0);
                    if(ix.data(Qt::DisplayRole).toString()==filename) {
                        folder=ix;break;
                    }
                }
                okay=folder.isValid() && sameIcon(
                    qvariant_cast<QIcon>(folder.data(Qt::DecorationRole)),
                    tc.folderArt);
                if(!okay)break;
                view->browseTo(tc.container);
                QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                okay=grid->model()->rowCount()>0;
                for(int row=0;okay && row<grid->model()->rowCount();++row) {
                    auto ix=grid->model()->index(row,0);
                    okay=sameIcon(qvariant_cast<QIcon>(
                        ix.data(Qt::DecorationRole)),tc.fileArt);
                    if(!okay)break;
                    grid->setCurrentIndex(ix);
                    QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                    auto expected=QIcon(tc.fileArt).pixmap(130,130).toImage();
                    okay=!details->pixmap(Qt::ReturnByValue).isNull() &&
                         details->pixmap(Qt::ReturnByValue).toImage()==expected &&
                         view->selectedAsset().startsWith(tc.container+"@");
                }
                if(!okay)break;
                // List mode reuses the same per-entry QIcon decoration.
                bool toggled=false;
                for(auto* button:view->findChildren<QToolButton*>()) {
                    if(button->toolTip()!="Toggle grid/list view")continue;
                    button->click();
                    toggled=true;
                    okay=grid->viewMode()==QListView::ListMode;
                    for(int row=0;okay && row<grid->model()->rowCount();++row)
                        okay=sameIcon(qvariant_cast<QIcon>(
                            grid->model()->index(row,0).data(
                                Qt::DecorationRole)),tc.fileArt);
                    button->click();
                    okay=okay && grid->viewMode()==QListView::IconMode;
                    break;
                }
                okay=okay&&toggled;
                if(okay)++verified;
            }
            std::cout<<"YMT/YCD embedded skeleton/animation file icons: "
                     <<(okay?"PASS":"FAIL")<<" ("<<verified<<"/2 dictionaries)\n";
            return okay?0:12;
        }
        if(parser.isSet(categoryIconsSmoke)) {
            auto* grid=view->findChild<QListView*>("assetGrid");
            auto* detail=view->findChild<QLabel*>("assetPreviewIcon");
            bool okay=grid && grid->model() && detail &&
                view->recordCount()==2;
            auto const sameIcon=[](QIcon const& actual,QString const& resource) {
                QIcon expected(resource);
                return !actual.isNull() && !expected.isNull() &&
                    actual.pixmap(128,128).toImage()==
                    expected.pixmap(128,128).toImage();
            };
            struct Case {QString filename,art;};
            std::array<Case,2> const cases{{
                {"sample.unknown",":/northstar/icons/assetBrowser/otherFile.png"},
                {"sample.material-function.json",":/northstar/icons/assetBrowser/materialFunc.png"}
            }};
            for(auto const& tc:cases) {
                if(!okay)break;
                QModelIndex selected;
                auto* model=grid->model();
                for(int i=0;i<model->rowCount();++i) {
                    auto ix=model->index(i,0);
                    if(ix.data(Qt::DisplayRole).toString()==tc.filename) {
                        selected=ix;break;
                    }
                }
                okay=selected.isValid() &&
                    sameIcon(qvariant_cast<QIcon>(
                        selected.data(Qt::DecorationRole)),tc.art);
                if(!okay)break;
                grid->setCurrentIndex(selected);
                QCoreApplication::processEvents(QEventLoop::AllEvents,50);
                QIcon expected(tc.art);
                okay=!detail->pixmap(Qt::ReturnByValue).isNull() &&
                     detail->pixmap(Qt::ReturnByValue).toImage()==
                     expected.pixmap(130,130).toImage();
            }
            if(okay) {
                for(auto* button:view->findChildren<QToolButton*>()) {
                    if(button->toolTip()!="Toggle grid/list view")continue;
                    button->click();
                    okay=grid->viewMode()==QListView::ListMode;
                    for(int i=0;okay && i<grid->model()->rowCount();++i) {
                        auto ix=grid->model()->index(i,0);
                        QString const file=ix.data(Qt::DisplayRole).toString();
                        auto expected=file=="sample.unknown"?
                            ":/northstar/icons/assetBrowser/otherFile.png":
                            ":/northstar/icons/assetBrowser/materialFunc.png";
                        okay=sameIcon(
                            qvariant_cast<QIcon>(ix.data(Qt::DecorationRole)),
                            expected);
                    }
                    button->click();
                    okay=okay && grid->viewMode()==QListView::IconMode;
                    break;
                }
            }
            std::cout<<"Qt Other and Material Function PNG artwork: "
                     <<(okay?"PASS":"FAIL")<<" (grid/list/details)\n";
            return okay?0:11;
        }
        if(parser.isSet(folderIconsSmoke)) {
            auto* grid=view->findChild<QListView*>("assetGrid");
            auto* tree=view->findChild<QTreeWidget*>("folderTree");
            auto* details=view->findChild<QLabel*>("assetPreviewIcon");
            bool okay=grid && grid->model() && tree && details;
            auto sameIcon=[](QIcon const& actual,QString const& art) {
                QIcon original(art);
                return !actual.isNull() && !original.isNull() &&
                    actual.pixmap(128,128).toImage()==
                    original.pixmap(128,128).toImage();
            };
            struct FolderCase {
                QString folder;
                QString parent;
                QString display;
                QString image;
            };
            auto const cases=std::array<FolderCase,6>{{
                {"textures","","textures",":/northstar/icons/assetBrowser/folder.png"},
                {"models","","models",":/northstar/icons/assetBrowser/folder.png"},
                {"materials","","materials",":/northstar/icons/assetBrowser/folder.png"},
                {"textures/skydome.ytd","textures","skydome.ytd",
                    ":/northstar/icons/assetBrowser/texturesFolder.png"},
                {"models/skydome.ydd","models","skydome.ydd",
                    ":/northstar/icons/assetBrowser/modelsFolder.png"},
                {"materials/skydome.ymat","materials","skydome.ymat",
                    ":/northstar/icons/assetBrowser/materialsFolder.png"}
            }};
            int checked=0;
            for(auto const& test:cases) {
                if(!okay)break;
                view->browseTo(test.parent);
                auto const* model=grid->model();
                QModelIndex selected;
                for(int row=0;row<model->rowCount();++row) {
                    auto index=model->index(row,0);
                    if(index.data(Qt::DisplayRole).toString()==test.display) {
                        selected=index;break;
                    }
                }
                okay=selected.isValid() &&
                    sameIcon(qvariant_cast<QIcon>(selected.data(Qt::DecorationRole)),
                             test.image);
                if(!okay)break;
                grid->setCurrentIndex(selected);
                QCoreApplication::processEvents(QEventLoop::AllEvents,30);
                auto pixmap=details->pixmap(Qt::ReturnByValue);
                QIcon expected(test.image);
                okay=!pixmap.isNull() &&
                    pixmap.toImage()==expected.pixmap(130,130).toImage();
                if(!okay)break;
                auto treeItems=tree->findItems(test.display,
                    Qt::MatchExactly|Qt::MatchRecursive);
                bool matched=false;
                for(auto* item:treeItems) {
                    if(item->data(0,Qt::UserRole+1).toString()==test.folder) {
                        matched=sameIcon(item->icon(0),test.image);
                        break;
                    }
                }
                okay=matched;
                if(okay)++checked;
            }
            if(okay) {
                QIcon normal(":/northstar/icons/assetBrowser/folder.png");
                QIcon textures(":/northstar/icons/assetBrowser/texturesFolder.png");
                QIcon models(":/northstar/icons/assetBrowser/modelsFolder.png");
                QIcon materials(":/northstar/icons/assetBrowser/materialsFolder.png");
                auto different=[](QIcon const& a,QIcon const& b) {
                    return a.pixmap(128,128).toImage()!=b.pixmap(128,128).toImage();
                };
                okay=different(normal,textures) &&
                     different(normal,models) &&
                     different(normal,materials) &&
                     different(textures,models) &&
                     different(textures,materials) &&
                     different(models,materials);
            }
            std::cout<<"Qt NEF8 folder artwork grid/tree/details: "
                     <<(okay?"PASS":"FAIL")<<" ("<<checked<<"/6 folders)\n";
            return okay?0:8;
        }
        if(parser.isSet(smoke)) {
            bool okay=true;
            if(parser.isSet(content)) {
                auto* grid=view->findChild<QListView*>("assetGrid");
                okay=grid && grid->model();
                view->browseTo("textures/skydome.ytd");
                okay=okay && view->visibleCount()==5;
                QStringList texturePaths;
                if(okay) {
                    for(int i=0;i<grid->model()->rowCount();++i) {
                        auto index=grid->model()->index(i,0);
                        grid->setCurrentIndex(index);
                        auto reference=view->selectedAsset();
                        okay=okay && reference.startsWith("textures/skydome.ytd@");
                        texturePaths.push_back("textures/skydome.ytd/"+
                                               reference.section('@',-1));
                    }
                    // Must prove all five decoded thumbnail images arrived,
                    // not merely that every list entry has a category icon.
                    QElapsedTimer images;
                    images.start();
                    while(images.elapsed()<12000) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents,30);
                        bool ready=true;
                        for(auto const& path:texturePaths)
                            ready=ready && !view->cachedThumbnail(path).isNull();
                        if(ready)break;
                        QThread::msleep(10);
                    }
                    for(auto const& path:texturePaths)
                        okay=okay && !view->cachedThumbnail(path).isNull();
                }
                view->browseTo("models/skydome.ydd");
                okay=okay && view->visibleCount()==1;
                if(okay) {
                    grid->setCurrentIndex(grid->model()->index(0,0));
                    auto reference=view->selectedAsset();
                    okay=reference.startsWith("models/skydome.ydd@");
                    QElapsedTimer images;
                    images.start();
                    while(images.elapsed()<12000 &&
                          view->cachedThumbnail("models/skydome.ydd/skydome_high").isNull()) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents,30);
                        QThread::msleep(10);
                    }
                    okay=okay && !view->cachedThumbnail(
                        "models/skydome.ydd/skydome_high").isNull();
                }
                // YMAT is also an NEF8 ListFile: each of the three materials
                // must have its own loaded sphere, not a fallback icon.
                view->browseTo("materials/skydome.ymat");
                okay=okay && view->visibleCount()==3;
                QStringList materialPaths;
                for(int j=0;okay && j<grid->model()->rowCount();++j) {
                    auto ix=grid->model()->index(j,0);
                    grid->setCurrentIndex(ix);
                    auto ref=view->selectedAsset();
                    okay=ref.startsWith("materials/skydome.ymat@");
                    materialPaths.push_back("materials/skydome.ymat/"+
                                             ref.section('@',-1));
                }
                if(okay) {
                    QElapsedTimer sphereWait;sphereWait.start();
                    while(sphereWait.elapsed()<20000) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents,30);
                        bool loaded=true;
                        for(auto const& entry:materialPaths)
                            loaded=loaded && !view->cachedThumbnail(entry).isNull();
                        if(loaded)break;
                        QThread::msleep(10);
                    }
                    for(auto const& entry:materialPaths)
                        okay=okay && !view->cachedThumbnail(entry).isNull();
                }
                if(okay && parser.isSet(previewModalSmoke)) {
                    int activations=0;
                    QObject::connect(view,
                        &northstarcpp::editor::QtContentBrowser::assetActivated,
                        &window,[&activations](QString const&){++activations;});
                    auto inspectDoubleClick=[&](QString const& folder,
                                                QString const& entry) {
                        view->browseTo(folder);
                        auto* model=grid->model();
                        QModelIndex index;
                        for(int j=0;j<model->rowCount();++j) {
                            auto test=model->index(j,0);
                            if(test.data(Qt::DisplayRole).toString()==entry) {
                                index=test;break;
                            }
                        }
                        if(!index.isValid())return false;
                        grid->setCurrentIndex(index);
                        bool seen=false,rendered=false;
                        QElapsedTimer timeout;timeout.start();
                        QTimer check;
                        check.setInterval(25);
                        QObject::connect(&check,&QTimer::timeout,&window,[&] {
                            auto* modal=qobject_cast<QDialog*>(
                                QApplication::activeModalWidget());
                            if(modal && folder.endsWith(".ymat") &&
                               modal->objectName()=="northstarMaterialAssetEditor") {
                                seen=modal->windowTitle().contains(folder+"@"+entry);
                                auto* sphere=modal->findChild<QLabel*>("materialAssetPreviewSphere");
                                if(seen && sphere && !sphere->pixmap().isNull()) {
                                    rendered=true;
                                    modal->accept();
                                }
                            } else if(modal && modal->objectName()=="assetPreviewDialog") {
                                seen=true;
                                auto* viewport=modal->findChild<QWidget*>("assetPreviewCanvas");
                                if(viewport && viewport->property("previewRendered").toBool()) {
                                    rendered=true;
                                    modal->accept();
                                }
                            }
                            if(timeout.elapsed()>15000) {
                                if(modal)modal->reject();
                            }
                        });
                        check.start();
                        bool invoked=QMetaObject::invokeMethod(
                            grid,"doubleClicked",Qt::DirectConnection,
                            Q_ARG(QModelIndex,index));
                        check.stop();
                        return invoked && seen && rendered;
                    };
                    okay=inspectDoubleClick("textures/skydome.ytd","moon_new") &&
                         inspectDoubleClick("models/skydome.ydd","skydome_high") &&
                         inspectDoubleClick("materials/skydome.ymat","sky_cloud_clear") &&
                         activations==0;
                }
            } else {
                okay=view->recordCount()==5 && view->visibleCount()==3;
                view->setSearchText("albedo");
                okay=okay&&view->visibleCount()==1;
                view->setSearchText("");
                view->setTypeFilter("Textures");
                okay=okay&&view->visibleCount()==2;
                view->setTypeFilter("All Assets");
            }
            QCoreApplication::processEvents();
            std::cout<<"Qt native content browser smoke: "
                     <<(okay?"PASS":"FAIL")<<"\n";
            return okay?0:5;
        }
        if(parser.isSet(screenshot)) {
            QCoreApplication::processEvents();
            if(!window.grab().save(parser.value(screenshot)))
                return 6;
            return 0;
        }
    }
    return app.exec();
}
