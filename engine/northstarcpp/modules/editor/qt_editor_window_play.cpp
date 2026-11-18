#include "qt_editor_window.hpp"
#include "qt_editor_window_paths.hpp"
#include "qt_native_ui_preview.hpp"
#include "qt_play_surface.hpp"
#include "qt_pie_protocol.hpp"
#include "qt_pie_command_state.hpp"
#include "qt_inspector_scroll.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "qt_editor_viewport.hpp"
#include "qt_content_browser.hpp"
#include "qt_camera_studio.hpp"
#include "qt_asset_preview_dialog.hpp"
#include "qt_material_asset_dialog.hpp"
#include "qt_prefab_preview.hpp"
#include "editor_prefab.hpp"
#include "qt_material_authoring.hpp"
#include "qt_asset_warnings.hpp"
#include "qt_material_instances.hpp"
#include "qt_material_graph_editor.hpp"
#include "qt_material_gpu_preview.hpp"
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QAbstractItemView>
#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/scene_component_registry.hpp>
#include <northstarcpp/geometry_scene.hpp>
#include <northstarcpp/provider_inspector.hpp>
#include <northstarcpp/skeletal_animation.hpp>
#include <QClipboard>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QImage>
#include <QPixmap>
#include <QCryptographicHash>
#include <QComboBox>
#include <QIcon>
#include <QProcess>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QPointer>
#include <QRegularExpression>
#include <QCloseEvent>
#include <QDir>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QDockWidget>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QHeaderView>
#include <QHash>
#include <QSet>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QVariantList>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QShortcut>
#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QSplitter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QWidget>
#include <QVector3D>
#include <algorithm>
#include <vector>
#include <iostream>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <string>


namespace northstarcpp::editor {
namespace fs=std::filesystem;
using namespace window_detail;
QString QtEditorWindow::playerExecutable() const {
    QString const env=qEnvironmentVariable("NORTHSTARCPP_PLAYER_EXECUTABLE");
    if(!env.isEmpty() && QFileInfo(env).isFile())
        return QFileInfo(env).canonicalFilePath();
    if(!configuredPlayerPath_.isEmpty() &&
       QFileInfo(configuredPlayerPath_).isFile())
        return QFileInfo(configuredPlayerPath_).canonicalFilePath();
    QDir const binary(QCoreApplication::applicationDirPath());
    for(QString const& candidate:QStringList{
        binary.filePath("northstarcpp_player.exe"),
        binary.filePath("../northstarcpp_player.exe"),
        binary.filePath("../bin/northstarcpp_player.exe"),
        binary.filePath("../../bin/northstarcpp_player.exe"),
        // Split distribution: Engine/bin/qt_tools beside GameClient/bin.
        binary.filePath("../../../GameClient/bin/northstarcpp_player.exe"),
        binary.filePath("../../GameClient/bin/northstarcpp_player.exe")}) {
        QFileInfo info(candidate);
        if(info.isFile())return info.canonicalFilePath();
    }
    return {};
}
void QtEditorWindow::choosePlayerExecutable() {
    auto file=QFileDialog::getOpenFileName(this,"Locate NorthStar Game Player",
        configuredPlayerPath_.isEmpty()?QCoreApplication::applicationDirPath():
                                        configuredPlayerPath_,
        "NorthStar Player (northstarcpp_player.exe);;Executable (*.exe)");
    if(file.isEmpty())return;
    QFileInfo info(file);
    if(!info.isFile() ||
       info.fileName().compare("northstarcpp_player.exe",Qt::CaseInsensitive)!=0) {
        QMessageBox::warning(this,"Invalid Player",
            "Select the actual NorthStarCpp Player (northstarcpp_player.exe).");
        return;
    }
    configuredPlayerPath_=info.canonicalFilePath();
    QSettings("KaylasSystems","NorthStarQtEditor").setValue(
        "Game/PlayerExecutable",configuredPlayerPath_);
    log("Configured Game Player: "+configuredPlayerPath_);
}
void QtEditorWindow::playGame() {
    if(pieEjected_)possessGame();
    else launchGame(true);
}
void QtEditorWindow::playGameStandalone() {launchGame(false);}
bool QtEditorWindow::sendPieCommand(wchar_t const* messageName) {
#ifdef _WIN32
    if(!embeddedPlay_ || !playSurface_ || !playerProcess_ ||
       playerProcess_->state()!=QProcess::Running || !messageName)
        return false;
    auto const child=reinterpret_cast<HWND>(playSurface_->nativePlayerHandle());
    DWORD pid=0;
    if(!child || !IsWindow(child) ||
       !GetWindowThreadProcessId(child,&pid) ||
       quint64(pid)!=quint64(playerProcess_->processId()))
        return false;
    UINT const command=RegisterWindowMessageW(messageName);
    DWORD_PTR applied=0;
    // Versioned pointer-free IPC runs on Player's game-window thread.
    if(!command || !SendMessageTimeoutW(child,command,0,0,
          SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&applied) || applied!=1) {
        log("Player control command not acknowledged");
        return false;
    }
    return true;
#else
    Q_UNUSED(messageName);
    return false;
#endif
}
void QtEditorWindow::updatePieControls() {
    if(!playAction_ || !stopAction_ || !pauseAction_ || !resumeAction_ ||
       !stepAction_ ||
       !ejectAction_)return;
    auto const availability=pie_commands({
        playerProcess_ && playerProcess_->state()!=QProcess::NotRunning,
        playSurface_ && playSurface_->playerAttached(),
        embeddedPlay_,piePaused_,pieEjected_});
    playAction_->setEnabled(availability.play);
    playAction_->setText(availability.possess?"Possess Game":"Play In Editor");
    playAction_->setToolTip(availability.possess?
        "Possess running game (F6)":"Play In Editor (F6)");
    stopAction_->setEnabled(availability.stop);
    pauseAction_->setEnabled(availability.pause);
    resumeAction_->setEnabled(availability.resume);
    stepAction_->setEnabled(availability.step);
    ejectAction_->setEnabled(availability.eject);
}
void QtEditorWindow::pauseGame() {
    if(piePaused_ || !sendPieCommand(pie_protocol::pause))
        return;
    piePaused_=true;
    updatePieControls();
    log("Play In Editor: simulation paused");
}
void QtEditorWindow::resumeGame() {
    if(!piePaused_ ||
       !sendPieCommand(pie_protocol::resume))
        return;
    piePaused_=false;
    updatePieControls();
    log("Play In Editor: simulation resumed");
}
void QtEditorWindow::stepGame() {
    if(!piePaused_ || !sendPieCommand(pie_protocol::step))return;
    log("Play In Editor: advanced exactly one 1/60 s simulation frame");
}
void QtEditorWindow::ejectGame() {
    if(pieEjected_ || !playSurface_ ||
       !sendPieCommand(pie_protocol::eject))
        return;
    pieEjected_=true;
    playSurface_->hide(); // keep Player HWND/process alive
    restoreEditorChromeAfterPlay(false);
    updatePieControls();
    log("Play In Editor: ejected, Player remains alive");
}
void QtEditorWindow::possessGame() {
    if(!pieEjected_ || !playSurface_ ||
       !sendPieCommand(pie_protocol::possess))
        return;
    hideEditorChromeForPlay();
    pieEjected_=false;
    playSurface_->setGeometry(rect());
    playSurface_->show();
    playSurface_->raiseAboveEditorChrome();
    playSurface_->setFocus();
    updatePieControls();
    log("Play In Editor: possessed without restarting Player");
}
void QtEditorWindow::launchGame(bool embedded) {
    if(!playerProcess_ || playerProcess_->state()!=QProcess::NotRunning) {
        log("Game player is already running");return;
    }
    if(file_.empty() || dirty_) {
        if(!saveScene()) {
            log("Game launch cancelled: save current scene first");return;
        }
    }
    if(file_.empty() || !fs::is_regular_file(file_)) {
        log("Game launch requires a saved project scene");return;
    }
    auto const executable=playerExecutable();
    if(executable.isEmpty()) {
        log("Game Player not found. Build northstarcpp_player or use "
            "Game > Set Player Executable...");
        QMessageBox::warning(this,"Game Player Not Found",
            "The NorthStarCpp Player executable could not be found.\n"
            "Build the northstarcpp_player target or select its path "
            "under Game > Set Player Executable...");
        return;
    }
    QStringList args{"--scene",qpath(fs::absolute(file_))};
    // Project <scene>/assets is mounted automatically by Player, at normal
    // project priority. NEVER pass it as --shared-assets: that option denotes
    // a separate low-priority engine-wide asset root. Passing the project
    // folder twice caused "Asset source already mounted" in engine.assets.
    QString sharedRoot=qpath(asset_paths::locate_shared_root(file_.parent_path()));
    if(sharedRoot.isEmpty())
        sharedRoot=qpath(asset_paths::locate_shared_root(native(executable)));
    if(!sharedRoot.isEmpty()) {
        QString const projectAssets=QFileInfo(folderRoot(file_)).canonicalFilePath();
        if(projectAssets.isEmpty() ||
           sharedRoot.compare(projectAssets,Qt::CaseInsensitive)!=0)
            args<<"--shared-assets"<<sharedRoot;
    }
    if(exploreModeAction_ && exploreModeAction_->isChecked())
        args<<"--explore";
    else if(fpsModeAction_ && fpsModeAction_->isChecked())
        args<<"--fps";
    if(embedded) {
#ifdef _WIN32
        auto* surface=beginPlayOverlay();
        if(!surface)return;
        args<<"--embed-parent" <<
            ("0x"+QString::number(qulonglong(surface->nativeParentHandle()),16));
#else
        QMessageBox::warning(this,"Play In Editor",
            "Native in-editor game windows currently require Windows.");
        return;
#endif
    }
    embeddedPlay_=embedded;
    playerProcess_->setProgram(executable);
    playerProcess_->setArguments(args);
    playerProcess_->setWorkingDirectory(QFileInfo(executable).absolutePath());
    playAction_->setEnabled(false);
    stopAction_->setEnabled(true);
    log("Starting game: "+executable+" "+args.join(" "));
    playerProcess_->start(QIODevice::ReadOnly);
}
void QtEditorWindow::stopGame() {
    if(!playerProcess_ || playerProcess_->state()==QProcess::NotRunning) {
        endPlayOverlay();
        return;
    }
    if(playSurface_)
        playSurface_->requestPlayerClose();
    else
        playerProcess_->terminate();
    QPointer<QProcess> process(playerProcess_);
    QTimer::singleShot(2500,this,[process] {
        if(process && process->state()!=QProcess::NotRunning)
            process->kill(); // never terminate unrelated user processes
    });
}
bool QtEditorWindow::runPlayerCheckSmoke(QString const& root) {
    QDir dir(root);
    if(!dir.mkpath("."))return false;
    auto scene=dir.filePath("launch-smoke.nscn");
    if(!saveScene(scene))return false;
    QString const player=playerExecutable();
    if(player.isEmpty()) {
        log("Player smoke cannot run without northstarcpp_player.exe");
        return false;
    }
    QProcess check;
    check.setProgram(player);
    check.setArguments({"--check-scene","--scene",scene});
    check.setWorkingDirectory(QFileInfo(player).absolutePath());
    check.start(QIODevice::ReadOnly);
    if(!check.waitForStarted(8000)) {
        log("Player startup failed: "+check.errorString());return false;
    }
    if(!check.waitForFinished(30000)) {
        check.kill();check.waitForFinished(2000);
        log("Player scene validation timed out");return false;
    }
    if(check.exitStatus()!=QProcess::NormalExit || check.exitCode()!=0) {
        log("Player validation failure: "+
            QString::fromUtf8(check.readAll()).right(500));
        return false;
    }
    return true;
}
void QtEditorWindow::hideEditorChromeForPlay() {
    playWorkspaceBefore_=saveState(1);
    playMenuWasVisible_=menuBar() && menuBar()->isVisible();
    playStatusWasVisible_=statusBar() && statusBar()->isVisible();
    for(auto* dock:findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly))
        dock->hide();
    for(auto* bar:findChildren<QToolBar*>(QString(),Qt::FindDirectChildrenOnly))
        bar->hide();
    if(menuBar())menuBar()->hide();
    if(statusBar())statusBar()->hide();
}
void QtEditorWindow::restoreEditorChromeAfterPlay(bool clearSnapshot) {
    if(!playWorkspaceBefore_.isEmpty()) {
        if(!restoreState(playWorkspaceBefore_,1))
            log("Play In Editor: workspace restore failed");
        if(clearSnapshot)playWorkspaceBefore_.clear();
    }
    if(menuBar())menuBar()->setVisible(playMenuWasVisible_);
    if(statusBar())statusBar()->setVisible(playStatusWasVisible_);
    if(viewport_)viewport_->setFocus();
}
QtPlaySurface* QtEditorWindow::beginPlayOverlay() {
    if(playSurface_)return playSurface_;
    // UE-style immersive play: store the entire current dock/toolbar layout,
    // then hide editor chrome. Merely raising a QWidget is not enough for
    // Qt6 native HWND dock/viewport siblings: they could remain above the
    // game's child swapchain and obscure the image.
    hideEditorChromeForPlay();

    // The native game surface is parented to the Editor, never to the
    // central Scene Viewport. It covers ALL editor client pixels.
    auto* overlay=new QtPlaySurface(this);
    overlay->setGeometry(rect());
    connect(overlay,&QtPlaySurface::pauseRequested,this,&QtEditorWindow::pauseGame);
    connect(overlay,&QtPlaySurface::resumeRequested,this,&QtEditorWindow::resumeGame);
    connect(overlay,&QtPlaySurface::stepRequested,this,&QtEditorWindow::stepGame);
    connect(overlay,&QtPlaySurface::ejectRequested,this,&QtEditorWindow::ejectGame);
    connect(overlay,&QtPlaySurface::exitRequested,this,[this] {
        stopGame(); // Escape is a single return to authoring, not a pause.
    });
    connect(overlay,&QtPlaySurface::playerAttachedChanged,this,
            [this](bool ready) {
        if(ready)log("Play In Editor: native game window attached to full editor");
        updatePieControls();
    });
    playSurface_=overlay;
    overlay->show();
    overlay->raiseAboveEditorChrome();
    overlay->setFocus();
    log("Play In Editor: immersive Editor client active");
    return overlay;
}
void QtEditorWindow::endPlayOverlay() {
    embeddedPlay_=false;
    bool const wasEjected=pieEjected_;
    piePaused_=false;
    pieEjected_=false;
    updatePieControls();
    if(!playSurface_)return;
    auto* overlay=playSurface_;
    playSurface_=nullptr;
    overlay->setPlayerProcessId(0);
    overlay->hide();
    overlay->deleteLater();

    // Restore exact dock positions, tab groups, visibility and toolbar
    // placement. Do not leave the Editor with hidden panels after Escape,
    // failed Player startup or a renderer crash.
    // Returning from an ejected session must preserve edits to the dock layout.
    if(wasEjected)playWorkspaceBefore_.clear();
    else restoreEditorChromeAfterPlay(true);
    log("Play In Editor: returned to authoring workspace");
}
void QtEditorWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if(playSurface_ && !pieEjected_) {
        playSurface_->setGeometry(rect());
        playSurface_->raiseAboveEditorChrome();
    }
}

bool QtEditorWindow::runEmbeddedRenderSmoke(QString const& executable,
                                            QString const& scene) {
#ifdef _WIN32
    if(!playerProcess_ || !QFileInfo(executable).isFile() ||
       !QFileInfo(scene).isFile() ||
       playerProcess_->state()!=QProcess::NotRunning)
        return false;
    auto* overlay=beginPlayOverlay();
    QPointer<QtPlaySurface> guarded=overlay;
    bool attached=false,escaped=false;
    connect(overlay,&QtPlaySurface::playerAttachedChanged,this,
            [&](bool ready){if(ready)attached=true;});
    connect(overlay,&QtPlaySurface::exitRequested,this,
            [&]{escaped=true;});
    QStringList arguments{"--scene",scene,"--warp","--embed-parent",
        "0x"+QString::number(qulonglong(overlay->nativeParentHandle()),16)};
    playerProcess_->setProgram(executable);
    // Resolve shared engine resources using the same logic as Play.
    QString const shared=qpath(asset_paths::locate_shared_root(native(executable)));
    if(!shared.isEmpty())arguments<<"--shared-assets"<<shared;
    playerProcess_->setArguments(arguments);
    playerProcess_->setWorkingDirectory(QFileInfo(executable).absolutePath());
    playerProcess_->start(QIODevice::ReadOnly);
    QElapsedTimer elapsed;elapsed.start();
    embeddedPlay_=true;
    qint64 attachedAt=-1;
    bool sentEscape=false;
    bool pausedOk=false,stepOk=false,resumedOk=false,ejectedOk=false,possessedOk=false;
    int controlPhase=0;
    while(elapsed.elapsed()<17500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,30);
        if(guarded && guarded->playerAttached() && attachedAt<0)
            attachedAt=elapsed.elapsed();
        if(attachedAt>=0 && playerProcess_->state()==QProcess::Running) {
            auto const time=elapsed.elapsed()-attachedAt;
            if(controlPhase==0 && time>=200) {
                auto const hwnd=reinterpret_cast<HWND>(guarded->nativePlayerHandle());
                if(hwnd)PostMessageW(hwnd,WM_KEYDOWN,VK_F7,1);
                ++controlPhase;
            } else if(controlPhase==1 && time>=400) {
                pausedOk=piePaused_;
                stepOk=pausedOk && stepAction_ && stepAction_->isEnabled() &&
                    sendPieCommand(pie_protocol::step);
                ++controlPhase;
            } else if(controlPhase==2 && time>=640) {
                resumeGame();resumedOk=!piePaused_ && pausedOk; ++controlPhase;
            } else if(controlPhase==3 && time>=820) {
                auto const hwnd=reinterpret_cast<HWND>(guarded->nativePlayerHandle());
                if(hwnd)PostMessageW(hwnd,WM_KEYDOWN,VK_F8,1);
                ++controlPhase;
            } else if(controlPhase==4 && time>=1080) {
                ejectedOk=pieEjected_ && viewport_->isVisible();
                possessGame(); possessedOk=!pieEjected_ &&
                    guarded && guarded->isVisible(); ++controlPhase;
            }
        }
        if(attachedAt>=0 && !sentEscape &&
           elapsed.elapsed()-attachedAt>=1700 &&
           playerProcess_->state()==QProcess::Running && guarded) {
            // The real renderer's surface has existed long enough to
            // receive frames; exercise Escape with focus inside Player.
            auto const player=reinterpret_cast<HWND>(
                guarded->nativePlayerHandle());
            if(player && IsWindow(player)) {
                PostMessageW(player,WM_KEYDOWN,VK_ESCAPE,1);
                sentEscape=true;
            }
        }
        if(playerProcess_->state()==QProcess::NotRunning &&
           (!guarded || !guarded->isVisible()))
            break;
        QThread::msleep(12);
    }
    bool const cleanExit=playerProcess_->state()==QProcess::NotRunning;
    if(!cleanExit) {
        playerProcess_->kill();
        playerProcess_->waitForFinished(1200);
        endPlayOverlay();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents,30);
    bool const restored=!playSurface_ && viewport_->isVisible() &&
                        playerProcess_->state()==QProcess::NotRunning;
    log(QString("PIE native render: attached=%1 escapeSent=%2 "
                "notified=%3 exited=%4 restored=%5")
        .arg(attached).arg(sentEscape).arg(escaped)
        .arg(cleanExit).arg(restored));
    std::cerr<<"PIE native render diagnostics: attached="<<attached
             <<" escapeSent="<<sentEscape<<" notified="<<escaped
             <<" exited="<<cleanExit<<" restored="<<restored<<std::endl;
    std::cerr<<"PIE controls: pause="<<pausedOk<<" step="<<stepOk<<" resume="<<resumedOk
             <<" eject="<<ejectedOk<<" possess="<<possessedOk<<std::endl;
    return attached && sentEscape && escaped && cleanExit && restored &&
           pausedOk && stepOk && resumedOk && ejectedOk && possessedOk;
#else
    Q_UNUSED(executable);Q_UNUSED(scene);
    return false;
#endif
}
bool QtEditorWindow::runEmbeddedPlaySmoke(QString const& executable) {
#ifdef _WIN32
    if(!QFileInfo(executable).isFile() || !playerProcess_ ||
       playerProcess_->state()!=QProcess::NotRunning)
        return false;
    auto const originalLayout=saveState(1);
    auto const menuBefore=menuBar() && menuBar()->isVisible();
    auto const statusBefore=statusBar() && statusBar()->isVisible();
    auto const docksBefore=findChildren<QDockWidget*>(
        QString(),Qt::FindDirectChildrenOnly);
    auto const toolbarsBefore=findChildren<QToolBar*>(
        QString(),Qt::FindDirectChildrenOnly);
    struct DockBefore {
        QDockWidget* dock;
        bool visible;
        Qt::DockWidgetArea area;
        QStringList tabs;
    };
    struct ToolbarBefore {
        QToolBar* bar;
        bool visible;
        Qt::ToolBarArea area;
    };
    std::vector<DockBefore> expectedDocks;
    std::vector<ToolbarBefore> expectedToolbars;
    for(auto* dock:docksBefore) {
        QStringList tabs;
        for(auto* peer:tabifiedDockWidgets(dock))
            tabs.push_back(peer->objectName());
        tabs.sort();
        expectedDocks.push_back({
            dock,dock->isVisible(),dockWidgetArea(dock),tabs});
    }
    for(auto* bar:toolbarsBefore)
        expectedToolbars.push_back({
            bar,bar->isVisible(),toolBarArea(bar)});
    auto* overlay=beginPlayOverlay();
    if(!overlay)return false;
    QPointer<QtPlaySurface> guarded=overlay;
    auto const handle=reinterpret_cast<HWND>(overlay->nativeParentHandle());
    bool const fullClient=IsWindow(handle) &&
        overlay->geometry()==rect() && overlay->isVisible() &&
        overlay->parentWidget()==this &&
        !overlay->testAttribute(Qt::WA_DontCreateNativeAncestors);
    auto allHidden=[](auto const& widgets) {
        return std::all_of(widgets.begin(),widgets.end(),[](auto* widget) {
            return !widget || !widget->isVisible();
        });
    };
    bool const editorChromeHidden=menuBar() && !menuBar()->isVisible() &&
        statusBar() && !statusBar()->isVisible() &&
        allHidden(docksBefore) && allHidden(toolbarsBefore);
    bool attached=false,escaped=false,unobstructed=false,playingLabelVisible=false;
    connect(overlay,&QtPlaySurface::playerAttachedChanged,this,
            [&](bool present){
                if(!present)return;
                attached=true;
                auto* label=overlay->findChild<QLabel*>("northstarPlayingFromEditor");
                playingLabelVisible=label && label->text()=="PLAYING FROM EDITOR" &&
                    label->isVisible() &&
                    IsWindow(reinterpret_cast<HWND>(label->winId())) &&
                    GetParent(reinterpret_cast<HWND>(label->winId()))==handle &&
                    label->testAttribute(Qt::WA_TransparentForMouseEvents);
                overlay->raiseAboveEditorChrome();
                RECT bounds{};
                if(!GetWindowRect(handle,&bounds))return;
                // Native hit testing at positions formerly occupied by
                // Scene Outliner, Inspector, ContentBrowser and menu. The
                // entire editor client must hit Player/overlay, not panes.
                std::array<POINT,5> const samples{{
                    {bounds.left+(bounds.right-bounds.left)/10,
                     bounds.top+(bounds.bottom-bounds.top)/10},
                    {bounds.left+9*(bounds.right-bounds.left)/10,
                     bounds.top+(bounds.bottom-bounds.top)/10},
                    {bounds.left+(bounds.right-bounds.left)/10,
                     bounds.top+9*(bounds.bottom-bounds.top)/10},
                    {bounds.left+9*(bounds.right-bounds.left)/10,
                     bounds.top+9*(bounds.bottom-bounds.top)/10},
                    {bounds.left+(bounds.right-bounds.left)/2,
                     bounds.top+(bounds.bottom-bounds.top)/2}
                }};
                auto const parent=GetParent(handle);
                unobstructed=std::all_of(samples.begin(),samples.end(),
                    [&](POINT sample) {
                        auto point=sample;
                        MapWindowPoints(nullptr,parent,&point,1);
                        // Test the Editor's native child z-order, independent
                        // of which unrelated desktop app has OS foreground.
                        auto const hit=ChildWindowFromPointEx(parent,point,
                            CWP_SKIPINVISIBLE|CWP_SKIPTRANSPARENT);
                        if(hit==handle || IsChild(handle,hit))return true;
                        wchar_t kind[120]{};
                        GetClassNameW(hit,kind,120);
                        std::cerr<<"PIE obscured sample parent="<<parent
                                 <<" surface="<<handle<<" hit="<<hit
                                 <<" class="<<QString::fromWCharArray(kind).toStdString()
                                 <<" x="<<point.x<<" y="<<point.y<<std::endl;
                        return false;
                    });
            });
    connect(overlay,&QtPlaySurface::exitRequested,this,
            [&]{escaped=true;});
    playerProcess_->setProgram(executable);
    playerProcess_->setArguments({"--embed-parent",
        "0x"+QString::number(qulonglong(overlay->nativeParentHandle()),16),
        "--embed-handshake-smoke"});
    playerProcess_->setWorkingDirectory(QFileInfo(executable).absolutePath());
    playerProcess_->start(QIODevice::ReadOnly);
    QElapsedTimer elapsed;elapsed.start();
    while(elapsed.elapsed()<6500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,30);
        if(playerProcess_->state()==QProcess::NotRunning &&
           (!guarded || !guarded->isVisible()))
            break;
        QThread::msleep(10);
    }
    if(playerProcess_->state()!=QProcess::NotRunning) {
        playerProcess_->kill();
        playerProcess_->waitForFinished(1600);
        endPlayOverlay();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents,30);
    bool const docksRestored=std::all_of(
        expectedDocks.begin(),expectedDocks.end(),
        [this](DockBefore const& original) {
            if(!original.dock ||
               original.dock->isVisible()!=original.visible ||
               dockWidgetArea(original.dock)!=original.area)
                return false;
            QStringList now;
            for(auto* peer:tabifiedDockWidgets(original.dock))
                now.push_back(peer->objectName());
            now.sort();
            return now==original.tabs;
        });
    bool const toolbarsRestored=std::all_of(
        expectedToolbars.begin(),expectedToolbars.end(),
        [this](ToolbarBefore const& original) {
            return original.bar &&
                original.bar->isVisible()==original.visible &&
                toolBarArea(original.bar)==original.area;
        });
    bool const restored=!playSurface_ &&
        playerProcess_->state()==QProcess::NotRunning &&
        viewport_ && viewport_->isVisible() && !viewport_->isHidden() &&
        (!menuBar() || menuBar()->isVisible()==menuBefore) &&
        (!statusBar() || statusBar()->isVisible()==statusBefore) &&
        docksRestored && toolbarsRestored;
    if(!restored)
        std::cerr<<"PIE restore details viewportVisible="
                 <<(viewport_ && viewport_->isVisible())
                 <<" menu="<<(menuBar() && menuBar()->isVisible())
                 <<" menuBefore="<<menuBefore
                 <<" status="<<(statusBar() && statusBar()->isVisible())
                 <<" statusBefore="<<statusBefore
                 <<" docksRestored="<<docksRestored
                 <<" toolbarsRestored="<<toolbarsRestored
                 <<" stateBytesIdentical="<<(saveState(1)==originalLayout)
                 <<" playerState="<<int(playerProcess_->state())
                 <<std::endl;
    if(!restored)endPlayOverlay();
    auto const diagnostic=QString(
        "PIE IPC smoke: full_client=%1 chrome_hidden=%2 "
        "attached=%3 unobstructed=%4 escape=%5 restored=%6 "
        "label=%7 player_exit=%8 player_error=%9")
        .arg(fullClient).arg(editorChromeHidden).arg(attached)
        .arg(unobstructed).arg(escaped).arg(restored)
        .arg(playingLabelVisible).arg(playerProcess_->exitCode())
        .arg(playerProcess_->errorString());
    log(diagnostic);
    std::cerr<<diagnostic.toStdString()<<std::endl;
    return fullClient && editorChromeHidden && attached &&
           unobstructed && playingLabelVisible && escaped && restored;
#else
    Q_UNUSED(executable);
    return false;
#endif
}
}
