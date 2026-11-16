#include "qt_editor_window.hpp"
#include "qt_place_actors_panel.hpp"
#include <northstarcpp/scene_camera_actor.hpp>
#include <northstarcpp/camera.hpp>
#include "../render/shared/render_light_components.hpp"
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QListWidget>
#include <northstarcpp/player_character_components.hpp>
#include "qt_actor_tree_widget.hpp"
#include "qt_editor_window_paths.hpp"
#include "qt_native_ui_preview.hpp"
#include "qt_play_surface.hpp"
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
#include "editor_visual_prefab.hpp"
#include "qt_material_authoring.hpp"
#include "qt_asset_warnings.hpp"
#include "qt_material_instances.hpp"
#include "qt_material_graph_editor.hpp"
#include "qt_material_gpu_preview.hpp"
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QAbstractItemView>
#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/shared_assets.hpp>
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
#include <QScreen>
#include <QWindow>
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
bool QtEditorWindow::runPlaceActorsSmoke() {
    try {
        if(!placeActors_ || !placeActorsDock_ ||
           !placeActors_->hasEntry("camera.free") ||
           !placeActors_->hasEntry("light.point") ||
           !placeActors_->hasEntry("light.spot") ||
           !placeActors_->hasEntry("geometry.torus"))
            throw std::runtime_error("Place Actors dock/registration missing");
        auto* search=placeActors_->findChild<QLineEdit*>("placeActorsSearch");
        auto* list=placeActors_->findChild<QListWidget*>("placeActorsList");
        auto* category=placeActors_->findChild<QComboBox*>("placeActorsCategory");
        if(!search || !list || !category)
            throw std::runtime_error("Place Actors search/list/category missing");
        search->setText("spot");
        if(list->count()!=1 ||
           list->item(0)->data(Qt::UserRole).toString()!="light.spot")
            throw std::runtime_error("Place Actors search did not filter");
        search->clear();

        editor_tools::promote_to_v3(scene_);
        auto const baseline=scene_.objects.size();
        placeActorById("camera.free",{2.f,0.f,3.f});
        if(scene_.objects.size()!=baseline+1)
            throw std::runtime_error("Camera actor was not created");
        auto const cameraId=scene_.objects.back().node.id;
        if(!scene_component_schema(scene_camera::type) ||
           !scene_camera::decode(scene_.components.back().payload))
            throw std::runtime_error("Camera component payload/schema missing");
        editScene([cameraId](Scene& scene) {
            for(auto& component:scene.components)
                if(component.entity_id==cameraId &&
                   component.type==scene_camera::type) {
                    scene_camera::Settings config;
                    config.active=1;config.priority=20;
                    component.payload=scene_camera::encode(config);
                    return;
                }
            throw std::runtime_error("Camera component record not found");
        });
        auto shot=scene_camera::active_shot(scene_);
        if(!shot || shot->priority!=20 ||
           std::abs(shot->pose.position.x-2.f)>.001f)
            throw std::runtime_error("Scene Camera Actor bridge invalid");
        CameraDirector director;
        CameraTarget target;target.eye={0.f,1.6f,0.f};
        director.update(1.f/60.f,target,&scene_);
        if(director.active_shot()!=shot->source)
            throw std::runtime_error("CameraDirector ignored Camera Actor");

        placeActorById("light.point",{1.f,0.f,2.f});
        placeActorById("light.spot",{4.f,0.f,5.f});
        if(!scene_component_schema("engine.render.light"))
            throw std::runtime_error("Light ECS schema missing");
        const auto lights=render::scene_render_lights(scene_);
        if(lights.size()!=2 || lights[0].direction_cone[3]>=0.f ||
           lights[1].direction_cone[3]<0.f ||
           std::abs(lights[0].position_range[1]-2.5f)>.001f)
            throw std::runtime_error("Point/Spot GPU light bridge invalid");
        auto searchOutliner=[this](QString const& filter,QString const& expected) {
            outlinerSearch_->setText(filter);
            bool found=false;
            for(QTreeWidgetItemIterator it(outliner_);*it;++it)
                if((*it)->text(0)==expected)found=true;
            outlinerSearch_->clear();
            return found;
        };
        if(!searchOutliner("type:camera","CameraActor") ||
           !searchOutliner("type:light","PointLight"))
            throw std::runtime_error("Camera/Light Outliner filtering missing");
        if(!placeActors_->recentIds().contains("light.spot") ||
           !placeActors_->recentIds().contains("camera.free"))
            throw std::runtime_error("Recent actor placement not recorded");
        bool const previouslyFavorite=
            placeActors_->favoriteIds().contains("light.spot");
        placeActors_->setFavorite("light.spot",!previouslyFavorite);
        QtPlaceActorsPanel reloaded;
        if(reloaded.favoriteIds().contains("light.spot")==previouslyFavorite)
            throw std::runtime_error("Favorites not persisted in QSettings");
        placeActors_->setFavorite("light.spot",previouslyFavorite);
        // New panels also load the persisted recent-actors history.
        if(!reloaded.recentIds().contains("light.spot"))
            throw std::runtime_error("Recent actors not persisted");

        // Provider-supplied ECS schemas are discoverable without modifying
        // the editor's built-in catalog or producing fake Actor proxies.
        auto const pluginType=std::string("com.northstar.qa.placeable");
        auto const pluginValidate=[](void*,std::uint32_t version,void const*,
                                      std::size_t length)->bool {
            return version==1 && length==0;
        };
        if(!register_scene_component_type({pluginType,1,1,pluginValidate,nullptr}))
            throw std::runtime_error("Plugin schema registration failed");
        placeActors_->refreshCatalog();
        auto const pluginId=QString("component.")+QString::fromStdString(pluginType);
        if(!placeActors_->hasEntry(pluginId))
            throw std::runtime_error("Plugin actor not discovered");
        auto const beforePlugin=scene_.objects.size();
        placeActorById(pluginId,{5.f,0.f,7.f});
        if(scene_.objects.size()!=beforePlugin+1 ||
           scene_.components.back().type!=pluginType)
            throw std::runtime_error("Plugin component placement failed");
        unregister_scene_component_type(pluginType);
        placeActors_->refreshCatalog();
        if(placeActors_->hasEntry(pluginId))
            throw std::runtime_error("Unregistered plugin retained in catalog");

        QMimeData mime;
        mime.setData("application/x-northstar-placeable-id","geometry.torus");
        QDragEnterEvent enter(QPoint(90,90),Qt::CopyAction,&mime,
                              Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(viewport_,&enter);
        if(!enter.isAccepted())
            throw std::runtime_error("Viewport rejected actor drag");
        auto const count=scene_.objects.size();
        QDropEvent drop(QPointF(90,90),Qt::CopyAction,&mime,
                        Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(viewport_,&drop);
        if(!drop.isAccepted() || scene_.objects.size()!=count+1 ||
           scene_.objects.back().name!="Torus")
            throw std::runtime_error("Drop did not insert Torus actor");
        undo();
        if(scene_.objects.size()!=count)
            throw std::runtime_error("Dropped actor is not undoable");
        return true;
    } catch(std::exception const& e) {
        log("Place Actors smoke: "+QString::fromUtf8(e.what()));
        return false;
    }
}

bool QtEditorWindow::runSmoke(QString const& base) {
    try {
        // QA must not depend on persisted, possibly collapsed dock sizes.
        // A 10-pixel Outliner viewport cannot accept the visibility-eye click.
        if(outlinerDock_) {
            outlinerDock_->show();
            resizeDocks({outlinerDock_},{240},Qt::Vertical);
            QCoreApplication::processEvents();
        }
        refreshProviderInspector();
        // PIE transport controls are a single icon strip backed by Qt
        // resources; verify all nine bundled 64x64 RGBA assets, not a
        // filesystem dependency that only works in a developer's cwd.
        auto* pieStrip=findChild<QWidget*>("pieControlGroup");
        const std::array<char const*,6> buttons{{
            "piePlayButton","piePauseButton","pieResumeButton",
            "pieStepButton",
            "pieStopButton","pieEjectButton"}};
        if(!pieStrip || pieStrip->findChildren<QToolButton*>().size()!=6 ||
           !playAction_ || !stopAction_ ||
           playAction_->icon().isNull() || stopAction_->icon().isNull() ||
           !playAction_->isEnabled() || stopAction_->isEnabled()) {
            log("smoke: PIE toolbar is missing, invalid or has wrong idle state");
            return false;
        }
        for(auto const* name:buttons)
            if(!pieStrip->findChild<QToolButton*>(name)) {
                log(QString("smoke: PIE control is missing: %1").arg(name));
                return false;
            }
        for(auto const* name:{"play.png","play_disabled_64.png",
                "pause-active.png","resume-active.png","resume-disabled.png",
                "stop-active.png","stop-disabled.png",
                "eject-active.png","eject-disabled.png"}) {
            auto const icon=QImage(QString(":/northstar/play/%1").arg(name));
            if(icon.size()!=QSize(64,64) || !icon.hasAlphaChannel()) {
                log(QString("smoke: PIE icon missing/invalid: %1").arg(name));
                return false;
            }
        }
        for(auto const* name:{"pauseGameAction","resumeGameAction",
                              "stepGameAction",
                              "ejectGameAction"}) {
            auto* action=findChild<QAction*>(name);
            if(!action || action->isEnabled() || action->icon().isNull()) {
                log(QString("smoke: unsupported PIE command must be disabled: %1")
                    .arg(name));
                return false;
            }
        }
        // Search must filter placement proxies without hiding shared Edit actions.
        auto verifyPlacement=[](QToolButton* button)->bool {
            if(!button || !button->menu())return false;
            auto* menu=button->menu();
            auto* search=menu->findChild<QLineEdit*>("actorPlacementSearch");
            auto* shapes=menu->findChild<QMenu*>("placeActorShapes");
            auto* gameplay=menu->findChild<QMenu*>("placeActorGameplay");
            if(!search || !shapes || !gameplay ||
               shapes->actions().size()<5 || gameplay->actions().size()<2)
                return false;
            search->setText("torus");
            bool found=false;
            for(auto* action:menu->actions())
                found|=action->text()=="Torus" && action->isVisible();
            if(shapes->menuAction()->isVisible() || !found)return false;
            search->setText("no_such_actor_type");
            bool hasResults=false;
            for(auto* action:menu->actions())
                hasResults|=action->text()=="Torus" && action->isVisible();
            search->clear();
            return !hasResults && shapes->menuAction()->isVisible() &&
                   gameplay->menuAction()->isVisible();
        };
        auto* mainPlace=findChild<QToolButton*>("createActorButton");
        auto* outlinerPlace=findChild<QToolButton*>("outlinerAdd");
        auto* playModes=findChild<QToolButton*>("playModeButton");
        if(!verifyPlacement(mainPlace) || !verifyPlacement(outlinerPlace) ||
           !playModes || !playModes->menu() ||
           !playModes->menu()->actions().contains(playAction_) ||
           !playModes->menu()->actions().contains(exploreModeAction_) ||
           !findChild<QToolButton*>("toolbarContentButton")) {
            log("smoke: authoring toolbar / placement palette regression");
            return false;
        }
        auto* collisionWire=findChild<QCheckBox*>("viewportCollisionWireframe");
        auto* collisionShape=findChild<QComboBox*>("collisionShape");
        if(!collisionShape || !findChild<QComboBox*>("collisionObjectType") ||
           !findChild<QSpinBox*>("collisionSplitsNum") ||
           !findChild<QPushButton*>("collisionGenerateFromMesh") ||
           collisionShape->count()!=5 ||
           colliderSplits_->maximum()!=16) {
            log("smoke: advanced collision authoring controls unavailable");
            return false;
        }
        if(!collisionWire || !collisionShape || colliderX_==nullptr ||
           !findChild<QAction*>("viewCollisionWireframe")) {
            log("smoke: collision wireframe or collider authoring UI is missing");
            return false;
        }
        collisionWire->setChecked(true);
        if(!viewport_->showCollisions()) {
            log("smoke: collision wireframe did not reach the viewport");
            return false;
        }
        collisionWire->setChecked(false);
        if(!providerDock_ || !providers_ || providers_->topLevelItemCount()<11) {
            log("smoke: Engine Providers panel failed to enumerate ABI services");
            return false;
        }
        // Skeleton authoring must remain exposed in the real Qt editor,
        // not only in the standalone animation library.
        auto* skeletonAction=findChild<QAction*>("inspectSkeletonRigAction");
        if(!skeletonAction || !skeletonAction->isEnabled()) {
            log("smoke: skeleton asset inspector action is missing");
            return false;
        }
        auto sampleRig=parse_skeleton_rig(
            R"({"schema":1,"type":"skeleton","bones":[{"name":"root","parent":-1}]})");
        if(sampleRig.bone_count()!=1 ||
           pack_skinning_palette(sampleRig.skinning_palette({},0.f)).size()!=16) {
            log("smoke: Editor skeleton bind palette validation failed");
            return false;
        }
        QDir dir(base);
        if(!dir.mkpath("assets/Meshes"))return false;
        auto file=dir.filePath("assets/Meshes/smoke.obj");
        QFile object(file);
        if(!object.open(QIODevice::WriteOnly))return false;
        object.write("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        object.close();
        auto sceneFile=dir.filePath("main.nscn");
        if(!saveScene(sceneFile) || !openScene(sceneFile)) {
            log("smoke: initial save/open failed");return false;
        }
        int before=objectCount();
        createActor("Meshes/smoke.obj",{2,0,4});
        if(objectCount()!=before+1){log("smoke: createActor count mismatch");return false;}
        if(scene_.objects.back().node.asset!="Meshes/smoke.obj"){log("smoke: wrong asset reference");return false;}
        if(!undoAction_->isEnabled()){log("smoke: undo disabled");return false;}
        undo();
        if(objectCount()!=before){log("smoke: undo count mismatch");return false;}
        redo();
        if(objectCount()!=before+1){log("smoke: createActor count mismatch");return false;}
        if(!saveScene() || !openScene(sceneFile)){log("smoke: roundtrip save/open failed");return false;}
        if(scene_.objects.back().node.asset!="Meshes/smoke.obj"){log("smoke: wrong asset reference");return false;}
        // UE-inspired Outliner: primary label + a fixed-width visibility eye,
        // no Type column or horizontal scrolling.
        if(!outliner_->isHeaderHidden() || outliner_->columnCount()!=2 ||
           outliner_->header()->sectionSize(1)!=32 ||
           outliner_->horizontalScrollBarPolicy()!=Qt::ScrollBarAlwaysOff ||
           !outlinerSearch_->isClearButtonEnabled() ||
           !outlinerDock_->findChild<QLabel*>("outlinerCount") ||
           !outlinerDock_->findChild<QToolButton*>("outlinerExpand") ||
           !outlinerDock_->findChild<QToolButton*>("outlinerCollapse") ||
           !outlinerDock_->findChild<QToolButton*>("outlinerSort") ||
           !outlinerDock_->findChild<QToolButton*>("outlinerAdd")) {
            log("smoke: World Outliner layout regression");return false;
        }
        // Assert the actual NSCN parent relationship is represented as a
        // nested World Outliner branch, including ancestor-aware filtering.
        int const actor=int(scene_.objects.size()-1);
        auto const parentId=scene_.objects.front().node.id;
        editScene([actor,parentId](Scene& scene) {
            scene.objects[std::size_t(actor)].node.parent=parentId;
            scene.objects[std::size_t(actor)].name="HierarchyEditorSmoke";
        },actor);
        auto locate=[this](int index)->QTreeWidgetItem* {
            QTreeWidgetItemIterator it(outliner_);
            while(*it) {
                if((*it)->data(0,Qt::UserRole).toInt()==index)return *it;
                ++it;
            }
            return nullptr;
        };
        auto* child=locate(actor);
        if(!child || !child->parent() ||
           child->parent()->data(0,Qt::UserRole).toInt()!=0) {
            log("smoke: World Outliner lost parent hierarchy");return false;
        }
        outlinerSearch_->setText("HierarchyEditorSmoke");
        child=locate(actor);
        if(!child || !child->parent() ||
           child->parent()->data(0,Qt::UserRole).toInt()!=0) {
            log("smoke: hierarchy search hid the matched actor parent");return false;
        }
        // Search supports combined qualifiers, and selection does not
        // recreate the item (important for inline edits and scroll position).
        outlinerSearch_->setText("type:mesh HierarchyEditorSmoke");
        if(!locate(actor)) {
            log("smoke: Outliner type qualifier rejected a mesh");return false;
        }
        outlinerSearch_->setText("type:player HierarchyEditorSmoke");
        if(locate(actor)) {
            log("smoke: Outliner type qualifier returned non-player");return false;
        }
        outlinerSearch_->clear();
        auto* stable=locate(actor);
        chooseObject(0);
        chooseObject(actor);
        if(locate(actor)!=stable) {
            log("smoke: selection rebuilt the Outliner unexpectedly");return false;
        }
        if(!stable || stable->icon(1).isNull() ||
           !(stable->flags()&Qt::ItemIsEditable)) {
            log("smoke: eye or inline rename is missing");return false;
        }
        stable->setText(0,"InlineRenameSmoke");
        QApplication::processEvents();
        if(scene_.objects[std::size_t(actor)].name!="InlineRenameSmoke") {
            log("smoke: inline rename failed to save");return false;
        }
        undo();
        if(scene_.objects[std::size_t(actor)].name!="HierarchyEditorSmoke") {
            log("smoke: inline rename is not undoable");return false;
        }
        chooseObject(actor);
        auto* first=locate(0);
        auto* second=locate(actor);
        if(!first || !second ||
           outliner_->selectionMode()!=QAbstractItemView::ExtendedSelection) {
            log("smoke: multi-selection unavailable");return false;
        }
        auto x0=scene_.objects[0].position.x;
        auto x1=scene_.objects[std::size_t(actor)].position.x;
        auto y0=scene_.objects[0].position.y;
        first->setSelected(true);
        second->setSelected(true);
        if(selectedActorIds_.size()!=2) {
            log("smoke: multi-selection lost");return false;
        }
        // Mixed-value Details: explicitly reapplying the primary actor's
        // unchanged value must still update all other selected actors.
        // No unrelated axis may be modified; Undo is one transaction.
        if(std::abs(x0-x1)>0.0001f) {
            if(!px_->property("mixedValues").toBool() ||
               !px_->suffix().contains('*')) {
                log("smoke: mixed position property is not indicated");
                return false;
            }
            QMetaObject::invokeMethod(px_,"editingFinished",Qt::DirectConnection);
            if(std::abs(scene_.objects[0].position.x-x1)>.001f ||
               std::abs(scene_.objects[std::size_t(actor)].position.x-x1)>.001f ||
               scene_.objects[0].position.y!=y0) {
                log("smoke: reapplying unchanged primary value skipped mixed actor");
                return false;
            }
            undo();
            if(scene_.objects[0].position.x!=x0 ||
               scene_.objects[std::size_t(actor)].position.x!=x1) {
                log("smoke: mixed-value batch Undo is not atomic");
                return false;
            }
        }
        // Tri-state visibility is a real mixed value, not an unchecked
        // primary actor. Clicking changes only visibility on all selections.
        auto const initialVisible0=scene_.objects[0].node.visible;
        auto const initialVisible1=scene_.objects[std::size_t(actor)].node.visible;
        bool createdVisibleMix=initialVisible0==initialVisible1;
        if(createdVisibleMix)editScene([actor](Scene& next) {
            next.objects[std::size_t(actor)].node.visible=
                !next.objects[0].node.visible;
        });
        if(visible_->checkState()!=Qt::PartiallyChecked) {
            log("smoke: mixed visibility was not shown as tri-state");
            return false;
        }
        visible_->click();
        if(scene_.objects[0].node.visible!=scene_.objects[std::size_t(actor)].node.visible ||
           scene_.objects[0].position.y!=y0) {
            log("smoke: mixed visibility batch click changed unrelated values");
            return false;
        }
        undo();
        if(scene_.objects[0].node.visible==scene_.objects[std::size_t(actor)].node.visible) {
            log("smoke: mixed visibility transaction failed to undo");
            return false;
        }
        if(createdVisibleMix)undo();
        if(scene_.objects[0].node.visible!=initialVisible0 ||
           scene_.objects[std::size_t(actor)].node.visible!=initialVisible1) {
            log("smoke: mixed visibility setup failed to restore originals");
            return false;
        }
        auto const batchX=x1+3.f;
        px_->setValue(batchX);
        QMetaObject::invokeMethod(px_,"editingFinished",Qt::DirectConnection);
        if(std::abs(scene_.objects[0].position.x-batchX)>.001f ||
           std::abs(scene_.objects[std::size_t(actor)].position.x-batchX)>.001f ||
           scene_.objects[0].position.y!=y0) {
            log(QString("smoke: batch transform failed: x0=%1 x1=%2 expected=%3 y0=%4 actualY0=%5 selection=%6 dirty=%7")
                .arg(scene_.objects[0].position.x)
                .arg(scene_.objects[std::size_t(actor)].position.x)
                .arg(batchX).arg(y0).arg(scene_.objects[0].position.y)
                .arg(selectedActorIds_.size()).arg(inspectorDirtyScalars_.size()));
            return false;
        }
        undo();
        if(scene_.objects[0].position.x!=x0 ||
           scene_.objects[std::size_t(actor)].position.x!=x1) {
            log("smoke: batch undo not atomic");return false;
        }
        chooseObject(0);
        outliner_->expandAll();
        auto* eyeItem=locate(actor);
        if(!eyeItem) {
            log("smoke: target actor disappeared before eye click");return false;
        }
        outliner_->scrollToItem(eyeItem);
        auto const eyeRect=outliner_->visualItemRect(eyeItem);
        QPoint eyePoint(outliner_->header()->sectionViewportPosition(1)+16,
                        eyeRect.center().y());
        if(eyeRect.isEmpty() || !outliner_->viewport()->rect().contains(eyePoint)) {
            log(QString("smoke: visibility eye is outside the tree viewport "
                "rect=(%1,%2,%3,%4) viewport=(%5,%6) eye=(%7,%8) header1=%9")
                .arg(eyeRect.x()).arg(eyeRect.y())
                .arg(eyeRect.width()).arg(eyeRect.height())
                .arg(outliner_->viewport()->width())
                .arg(outliner_->viewport()->height())
                .arg(eyePoint.x()).arg(eyePoint.y())
                .arg(outliner_->header()->sectionViewportPosition(1)));
            return false;
        }
        auto const oldVisibility=scene_.objects[std::size_t(actor)].node.visible;
        QMouseEvent eyeDown(QEvent::MouseButtonPress,
                            QPointF(eyePoint),QPointF(eyePoint),
                            QPointF(outliner_->viewport()->mapToGlobal(eyePoint)),
                            Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(outliner_->viewport(),&eyeDown);
        QApplication::processEvents();
        if(scene_.objects[std::size_t(actor)].node.visible==oldVisibility ||
           selection_!=0) {
            log("smoke: visibility eye did not toggle without selection");return false;
        }
        outlinerSearch_->setText("is:hidden HierarchyEditorSmoke");
        if(oldVisibility && !locate(actor)) {
            log("smoke: hidden actor filter failed");return false;
        }
        outlinerSearch_->clear();
        undo();
        if(scene_.objects[std::size_t(actor)].node.visible!=oldVisibility) {
            log("smoke: visibility toggle could not be undone");return false;
        }
        // Verify cumulative snap: two 1.2m pointer moves should reach
        // 2.0m when 1m snapping is explicitly enabled. The normal UI
        // defaults to smooth unsnapped authoring.
        auto const original=scene_.objects[std::size_t(actor)].position.x;
        chooseObject(actor);
        auto const previousSnap=gizmoOptions_.snap;
        gizmoOptions_.snap=true;
        viewport_->gizmoDragStarted();
        viewport_->gizmoDelta(0,0,1.2f);
        viewport_->gizmoDelta(0,0,1.2f);
        viewport_->gizmoDragFinished();
        gizmoOptions_.snap=previousSnap;
        if(std::abs(scene_.objects[std::size_t(actor)].position.x-original-2.f)>.001f) {
            log("smoke: gizmo drag did not move actor");return false;
        }
        undo();
        if(std::abs(scene_.objects[std::size_t(actor)].position.x-original)>.001f){
            log("smoke: grouped gizmo Undo failed");return false;
        }
        redo();
        if(std::abs(scene_.objects[std::size_t(actor)].position.x-original-2.f)>.001f){
            log("smoke: grouped gizmo Redo failed");return false;
        }
        // Discriminate save/reload failure from a true parent ID codec error.
        if(!saveScene()) {
            log("smoke: hierarchy roundtrip saveScene() failed");return false;
        }
        if(!openScene(sceneFile)) {
            log("smoke: hierarchy roundtrip openScene() failed: "+sceneFile);
            return false;
        }
        if(std::size_t(actor)>=scene_.objects.size()) {
            log(QString("smoke: hierarchy roundtrip missing actor index %1 count=%2")
                .arg(actor).arg(scene_.objects.size()));return false;
        }
        auto const actualParent=scene_.objects[std::size_t(actor)].node.parent;
        if(actualParent!=parentId) {
            log(QString("smoke: NEF8 parent mismatch expected=%1 actual=%2 actor=%3")
                .arg(qulonglong(parentId)).arg(qulonglong(actualParent))
                .arg(actor));return false;
        }
        // Project manifest must select FPS mode without a manually configured flag.
        QFile manifest(dir.filePath("northstarcpp.project.json"));
        if(!manifest.open(QIODevice::WriteOnly | QIODevice::Truncate))return false;
        manifest.write("{\"engine\":\"NorthStarCpp\",\"game_mode\":\"fps\"}");
        manifest.close();
        if(!openScene(sceneFile) || !fpsModeAction_->isChecked() ||
           exploreModeAction_->isChecked()){
            log("smoke: project FPS launch mode detection failed");return false;
        }
        // Drag/drop targets must be real registered layers in the NSCN.
        editScene([](Scene& scene) {
            scene.layers.push_back(SceneLayer{"EditorSmokeLayer",true});
        },actor);
        // Exercise the same actor drop callback used by the Outliner drag/drop.
        auto* tree=static_cast<ActorTreeWidget*>(outliner_);
        tree->moveActor(actor,-1,"EditorSmokeLayer");
        if(scene_.objects[std::size_t(actor)].node.parent!=0 ||
           scene_.objects[std::size_t(actor)].node.layer!="EditorSmokeLayer"){
            log("smoke: actor reparent to layer failed");return false;
        }
        undo();
        if(scene_.objects[std::size_t(actor)].node.parent!=parentId){
            log("smoke: hierarchy Undo failed");return false;
        }
        redo();
        if(scene_.objects[std::size_t(actor)].node.layer!="EditorSmokeLayer"){
            log("smoke: hierarchy Redo failed");return false;
        }
        // Camera bookmark actions must store and restore a precise pose.
        auto savedPose=viewport_->camera().pose;
        savedPose.position.x+=45.f;
        viewport_->setCameraPose(savedPose);
        QAction* saveBookmark=nullptr;
        QAction* recallBookmark=nullptr;
        for(auto* action:findChildren<QAction*>()) {
            if(action->text()=="Store Bookmark 1")saveBookmark=action;
            if(action->text()=="Go To Bookmark 1")recallBookmark=action;
        }
        if(!saveBookmark || !recallBookmark){
            log("smoke: camera bookmark actions missing");return false;
        }
        saveBookmark->trigger();
        auto otherPose=savedPose;
        otherPose.position.x+=70.f;
        viewport_->setCameraPose(otherPose);
        recallBookmark->trigger();
        if(std::abs(viewport_->camera().pose.position.x-
                    savedPose.position.x)>.001f) {
            log("smoke: camera bookmark recall failed");return false;
        }
        QSettings("KaylasSystems","NorthStarQtEditor").remove(
            sceneCameraKey(file_)+"/Bookmark1");
        // Unknown ECS components must remain byte-for-byte intact across
        // editing, undo/redo and NEF8 ListFile serialization.
        auto const entityId=scene_.objects[std::size_t(actor)].node.id;
        auto const componentsBefore=scene_.components;
        auto const unchangedComponents=[this,&componentsBefore] {
            if(scene_.components.size()!=componentsBefore.size())return false;
            for(std::size_t k=0;k<componentsBefore.size();++k) {
                auto const& left=componentsBefore[k];
                auto const& right=scene_.components[k];
                if(left.entity_id!=right.entity_id || left.type!=right.type ||
                   left.version!=right.version || left.payload!=right.payload)
                    return false;
            }
            return true;
        };
        std::string const opaqueBytes{char(0),char(0xff),char(0xa5),char(0x0a)};
        editScene([entityId,&opaqueBytes](Scene& scene) {
            scene.components.push_back(SceneComponentBlob{
                entityId,"com.kaylasystems.editor.smoke",17,opaqueBytes});
        },actor);
        if(scene_.components.size()!=componentsBefore.size()+1 || !components_ ||
           components_->topLevelItemCount()!=1 ||
           components_->topLevelItem(0)->text(3)!="Opaque / Unavailable") {
            log("smoke: plugin component inspector lost opaque record");return false;
        }
        undo();
        if(!unchangedComponents()) {
            log("smoke: component Undo failed");return false;
        }
        redo();
        if(scene_.components.size()!=componentsBefore.size()+1 ||
           scene_.components.back().payload!=opaqueBytes) {
            log("smoke: component Redo lost binary payload");return false;
        }
        if(!saveScene() || !openScene(sceneFile) ||
           scene_.components.size()!=componentsBefore.size()+1 ||
           scene_.components.back().payload!=opaqueBytes ||
           scene_.components.back().version!=17 ||
           scene_.components.back().entity_id!=entityId) {
            log(QString("smoke: NEF8 opaque ECS component roundtrip failed components=%1 expected=%2 backVersion=%3 backOwner=%4 expectedOwner=%5 bytesEqual=%6 bytes=%7").arg(scene_.components.size()).arg(componentsBefore.size()+1).arg(scene_.components.empty()?0:scene_.components.back().version).arg(scene_.components.empty()?0:scene_.components.back().entity_id).arg(entityId).arg(!scene_.components.empty() && scene_.components.back().payload==opaqueBytes).arg(scene_.components.empty()?0:scene_.components.back().payload.size()));return false;
        }
        // Exercise the actual modal hex editor and its OK validation handler,
        // not a direct scene mutation. Binary 00 and FF must survive Qt.
        chooseObject(actor);
        components_->setCurrentItem(components_->topLevelItem(0));
        bool dialogPopulated=false;
        QTimer::singleShot(0,this,[&dialogPopulated] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog)return;
            auto* payload=dialog->findChild<QPlainTextEdit*>("componentHexPayload");
            auto* version=dialog->findChild<QLineEdit*>("componentSchemaVersion");
            auto* buttons=dialog->findChild<QDialogButtonBox*>();
            if(!payload || !version || !buttons)return;
            payload->setPlainText("00 ff aa 7e");
            version->setText("18");
            dialogPopulated=true;
            buttons->button(QDialogButtonBox::Ok)->click();
        });
        editComponent(false);
        std::string const changedBytes{char(0),char(0xff),char(0xaa),char(0x7e)};
        if(!dialogPopulated || scene_.components.size()!=componentsBefore.size()+1 ||
           scene_.components.back().payload!=changedBytes ||
           scene_.components.back().version!=18) {
            log("smoke: raw ECS component dialog failed");return false;
        }
        undo();
        if(scene_.components.size()!=componentsBefore.size()+1 ||
           scene_.components.back().payload!=opaqueBytes ||
           scene_.components.back().version!=17) {
            log("smoke: component dialog Undo failed");return false;
        }
        redo();
        if(scene_.components.size()!=componentsBefore.size()+1 ||
           scene_.components.back().payload!=changedBytes ||
           scene_.components.back().version!=18) {
            log("smoke: component dialog Redo failed");return false;
        }
        if(!saveScene() || !openScene(sceneFile) ||
           scene_.components.size()!=componentsBefore.size()+1 ||
           scene_.components.back().payload!=changedBytes ||
           scene_.components.back().version!=18) {
            log("smoke: component dialog NEF8 roundtrip failed");return false;
        }
        // Real Qt authoring path and NSCN serialization contract.
        auto const previousActorCount=scene_.objects.size();
        auto const previousComponentCount=scene_.components.size();
        createProceduralActor("Torus",
            {sizeof(NorthStarGeometryRequest),NORTHSTAR_GEOMETRY_TORUS,0,0,
             .65f,.24f,24,12});
        if(scene_.objects.size()!=previousActorCount+1 ||
           scene_.components.size()!=previousComponentCount+1 ||
           scene_.components.back().type!=geometry_primitive_component ||
           !scene_component_schema(geometry_primitive_component)) {
            log("smoke: geometry authoring or registry failed");return false;
        }
        auto const geometryId=scene_.objects.back().node.id;
        if(scene_.components.back().entity_id!=geometryId ||
           !saveScene() || !openScene(sceneFile) ||
           scene_.components.back().entity_id!=geometryId ||
           scene_.components.back().type!=geometry_primitive_component) {
            log("smoke: geometry NEF8 roundtrip failed");return false;
        }
        // Material is an asset reference, NOT a texture on a scene actor.
        // Exercise the actual Content->Drop->Material->Inspector->Undo/Save path.
        if(!dir.mkpath("assets/Textures"))return false;
        QImage sampleTexture(16,16,QImage::Format_ARGB32);
        sampleTexture.fill(QColor("#cf7b43"));
        if(!sampleTexture.save(dir.filePath("assets/Textures/editor-smoke.png")))
            return false;
        chooseObject(actor);
        handleAssetDrop("Textures/editor-smoke.png",{0,1,0},actor);
        auto const createdMaterial=from8(editor_tools::actor_material(
            scene_,std::size_t(actor)));
        qWarning()<<"material smoke ref"<<createdMaterial
                  <<"picker"<<materialPicker_->currentData().toString()
                  <<"recognition"<<material_authoring::is_material(createdMaterial)
                  <<"catalog"<<int(kind_from_extension(fs::path(utf8(createdMaterial))))
                  <<"components"<<scene_.components.size()
                  <<"file"<<QFileInfo::exists(
                       QDir(folderRoot(file_)).filePath(createdMaterial));
        if(!material_authoring::is_material(createdMaterial) ||
           materialPicker_->currentData().toString()!=createdMaterial ||
           kind_from_extension(fs::path(utf8(createdMaterial)))!=AssetKind::material) {
            log(QString("smoke material failed: reference='%1', picker='%2', recognized=%3, kind=%4, components=%5")
                .arg(createdMaterial,materialPicker_->currentData().toString())
                .arg(material_authoring::is_material(createdMaterial))
                .arg(int(kind_from_extension(fs::path(utf8(createdMaterial)))))
                .arg(scene_.components.size()));
            return false;
        }
        auto const contentRoot=native(folderRoot(file_));
        auto descriptor=material_authoring::load_descriptor(contentRoot,createdMaterial);
        auto const importedTexture=descriptor.value("base_color_texture").toString();
        if(!importedTexture.startsWith("Textures/") ||
           !importedTexture.contains(".ytd@editor-smoke") ||
           !QFileInfo::exists(QDir(folderRoot(file_)).filePath(
               importedTexture.left(importedTexture.indexOf('@')))) ||
           !QFileInfo::exists(QDir(folderRoot(file_)).filePath(createdMaterial))) {
            log("smoke: texture was not bound to material Base Color");return false;
        }
        undo();
        if(!editor_tools::actor_material(scene_,std::size_t(actor)).empty()) {
            log("smoke: material assignment Undo failed");return false;
        }
        redo();
        if(from8(editor_tools::actor_material(scene_,std::size_t(actor)))!=createdMaterial) {
            log("smoke: material assignment Redo failed");return false;
        }
        if(!saveScene() || !openScene(sceneFile) ||
           from8(editor_tools::actor_material(scene_,std::size_t(actor)))!=createdMaterial) {
            log("smoke: material binding was not serialized");return false;
        }
        chooseObject(actor);
        if(materialPicker_->currentData().toString()!=createdMaterial) {
            log("smoke: inspector did not restore selected material");return false;
        }
        // Edit the material descriptor itself without modifying scene actor
        // texture pointers (the actor has none). Read after atomic save.
        bool materialEditorSaved=false;
        QTimer::singleShot(0,this,[&materialEditorSaved] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog || dialog->objectName()!="northstarMaterialEditor")return;
            auto* rough=dialog->findChild<QDoubleSpinBox*>("material_roughness");
            auto* buttons=dialog->findChild<QDialogButtonBox*>();
            if(!rough || !buttons)return;
            rough->setValue(.27);
            materialEditorSaved=true;
            buttons->button(QDialogButtonBox::Save)->click();
        });
        openMaterialEditor(createdMaterial);
        descriptor=material_authoring::load_descriptor(contentRoot,createdMaterial);
        if(!materialEditorSaved ||
           std::abs(descriptor.value("roughness").toDouble()-0.27)>.0001) {
            log("smoke: Material Editor modal save did not persist roughness");return false;
        }
        // Existing material selection uses the same scene component; only
        // texture drops generate a new descriptor.
        auto const anotherMaterial=material_authoring::make_material_for_texture(
            contentRoot,"Textures/editor-smoke.png");
        refreshMaterialChoices(true);
        updateInspector();
        auto const option=materialPicker_->findData(anotherMaterial);
        if(option<1) {
            log("smoke: material picker omitted existing material");return false;
        }
        materialPicker_->setCurrentIndex(option);
        if(from8(editor_tools::actor_material(scene_,std::size_t(actor)))!=anotherMaterial) {
            log("smoke: choosing material did not assign it");return false;
        }
        handleAssetDrop(createdMaterial,{0,1,0},actor);
        if(from8(editor_tools::actor_material(scene_,std::size_t(actor)))!=createdMaterial) {
            log("smoke: dropping existing material failed");return false;
        }
        materialPicker_->setCurrentIndex(0);
        if(!editor_tools::actor_material(scene_,std::size_t(actor)).empty()) {
            log("smoke: None material must clear binding");return false;
        }
        undo();
        if(from8(editor_tools::actor_material(scene_,std::size_t(actor)))!=createdMaterial){
            log("smoke: undo clear material failed");return false;
        }
        // Shared YMAT assets must appear alongside project descriptors.
        // The picker never edits the original encoded resource.
        auto const shared=sharedMaterialRoot();
        if(!shared.empty() &&
           availableMaterial(shared,"characters/abby/materials/abby.ymat")) {
            refreshMaterialChoices(true);
            updateInspector();
            auto const library=QString("characters/abby/materials/abby.ymat");
            auto const available=material_authoring::list_materials(shared);
            QString selected;
            for(auto const& candidate:available)
                if(candidate==library || candidate.startsWith(library+"@")) {
                    selected=candidate;break;
                }
            auto const index=materialPicker_->findData(selected);
            if(selected.isEmpty() || index<1) {
                log("smoke: shared YMAT entries missing from material picker");
                return false;
            }
            materialPicker_->setCurrentIndex(index);
            if(editor_tools::actor_material(scene_,std::size_t(actor))!=
               utf8(selected)) {
                log("smoke: shared YMAT selection did not persist");return false;
            }
            undo();
            if(editor_tools::actor_material(scene_,std::size_t(actor))!=
               utf8(createdMaterial)) {
                log("smoke: shared material Undo failed");return false;
            }
        }

        // Material picker regression: two PBR descriptors must produce
        // visibly different studio spheres, and a texture-driven descriptor
        // must actually sample its authored Base Color texture.
        namespace ma=material_authoring;
        auto const content=native(dir.filePath("assets"));
        QJsonObject red{{"schema",1},{"name","Red"},{"shader","standard_lit"},
            {"base_color","#d93630"},{"metallic",0.0},{"roughness",0.60},
            {"base_color_texture",""}};
        QJsonObject blue=red;
        blue.insert("name","Blue");
        blue.insert("base_color","#286adb");
        ma::save_descriptor(content,"Materials/Red.material.json",red);
        ma::save_descriptor(content,"Materials/Blue.material.json",blue);
        auto const redSphere=ma::sphere_preview(
            content,"Materials/Red.material.json",96);
        auto const blueSphere=ma::sphere_preview(
            content,"Materials/Blue.material.json",96);
        if(redSphere.isNull() || blueSphere.isNull() ||
           redSphere.size()!=QSize(96,96) ||
           qRed(redSphere.pixel(48,48))<=qBlue(redSphere.pixel(48,48)) ||
           qBlue(blueSphere.pixel(48,48))<=qRed(blueSphere.pixel(48,48)) ||
           redSphere.pixel(48,48)==blueSphere.pixel(48,48) ||
           redSphere.pixel(0,0)!=blueSphere.pixel(0,0)) {
            log("smoke: sphere material PBR color preview mismatch");return false;
        }
        if(!dir.mkpath("assets/Textures"))return false;
        QImage checker(64,64,QImage::Format_RGBA8888);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)
            checker.setPixelColor(x,y,
                (((x/8)+(y/8))%2)==0?QColor("#ea9e24"):QColor("#2959b8"));
        if(!checker.save(dir.filePath("assets/Textures/checker.png")))return false;
        auto const textured=ma::make_material_for_texture(
            content,"Textures/checker.png");
        auto const textureSphere=ma::sphere_preview(content,textured,96);
        if(textureSphere.isNull() || textureSphere.pixel(48,48)==
           redSphere.pixel(48,48)) {
            log("smoke: texture-driven material sphere preview missing");return false;
        }
        refreshMaterialChoices(true);
        if(!materialPicker_ ||
           materialPicker_->findData("Materials/Red.material.json")<0 ||
           materialPicker_->findData("Materials/Blue.material.json")<0 ||
           materialPicker_->findData(textured)<0) {
            log("smoke: Inspector material picker lost project entries");return false;
        }
        int const materialActor=static_cast<int>(scene_.objects.size()-1);
        chooseObject(materialActor);
        assignMaterial(materialActor,"Materials/Red.material.json");
        if(materialPicker_->currentData().toString()!="Materials/Red.material.json") {
            log("smoke: Material picker does not follow selected scene actor");
            return false;
        }
        auto const blueIndex=materialPicker_->findData(
            "Materials/Blue.material.json");
        materialPicker_->setCurrentIndex(blueIndex); // Actual Qt change signal.
        if(editor_tools::actor_material(scene_,std::size_t(materialActor))!=
           "Materials/Blue.material.json") {
            log("smoke: Material picker did not assign asset");return false;
        }
        undo();
        if(editor_tools::actor_material(scene_,std::size_t(materialActor))!=
           "Materials/Red.material.json") {
            log("smoke: Material choice Undo failed");return false;
        }
        redo();
        if(editor_tools::actor_material(scene_,std::size_t(materialActor))!=
           "Materials/Blue.material.json" ||
           !saveScene() || !openScene(sceneFile) ||
           editor_tools::actor_material(scene_,std::size_t(materialActor))!=
           "Materials/Blue.material.json") {
            log("smoke: Material choice NSCN save/load failed");return false;
        }
        // Verify async worker icon updates, not just the generated preview.
        QElapsedTimer timer;
        timer.start();
        while(!materialSpheres_.contains("Materials/Blue.material.json") &&
              timer.elapsed()<6500) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        auto const entry=materialPicker_->findData(
            "Materials/Blue.material.json");
        if(!materialSpheres_.contains("Materials/Blue.material.json") ||
           entry<1 || materialPicker_->itemIcon(entry).isNull()) {
            log("smoke: async material sphere icon missing from picker");
            return false;
        }
        auto* addCharacter=findChild<QAction*>("addCharacterPawnAction");
        if(!addCharacter) {
            log("smoke: Character creation action missing");return false;
        }
        auto const beforeCharacters=scene_.objects.size();
        auto const beforeCharacterComponents=scene_.components.size();
        addCharacter->trigger();
        if(scene_.objects.size()!=beforeCharacters+1 ||
           scene_.components.size()<=beforeCharacterComponents ||
           !player_character::has_pawn(scene_,scene_.objects.back().node.id) ||
           !collision_authoring::lookup(scene_,scene_.objects.back().node.id)) {
            log("smoke: editor Character archetype creation failed");
            return false;
        }
        undo();
        if(scene_.objects.size()!=beforeCharacters) {
            log("smoke: Character archetype Undo failed");return false;
        }
        redo();
        if(scene_.objects.size()!=beforeCharacters+1 ||
           !player_character::has_pawn(scene_,scene_.objects.back().node.id)) {
            log("smoke: Character archetype Redo failed");return false;
        }
        return outlinerDock_ && detailsDock_ && contentDock_ && dependencyDock_ &&
               browser_ && viewport_;
    } catch(std::exception const& e){log(from8(e.what()));return false;}
}

bool QtEditorWindow::runVisualPrefabGpuSmoke() {
#ifdef _WIN32
    try {
        if(file_.empty() || scene_.version!=3)
            throw std::runtime_error("Specify a valid Scene V3 using --scene");
        if(qEnvironmentVariableIsSet("NORTHSTARCPP_QA_ISOLATE_VISUAL")) {
            auto const originals=visual_prefab::links(scene_);
            if(originals.empty())
                throw std::runtime_error("No visual prefab to isolate");
            auto it=std::find_if(scene_.objects.begin(),scene_.objects.end(),
                [&](SceneObject const& o){return o.node.id==originals.front().first;});
            if(it==scene_.objects.end())
                throw std::runtime_error("Visual prefab owner is missing");
            Scene isolated;isolated.version=3;isolated.name="Visual Prefab GPU QA";
            auto actor=*it;
            actor.node.parent=0;
            actor.node.collision=false;
            actor.mass=0.f;
            actor.position={0.f,0.f,0.f};
            isolated.objects.push_back(std::move(actor));
            for(auto const& c:scene_.components)
                if(c.entity_id==originals.front().first &&
                   (c.type==player_blueprint::mesh_type ||
                    c.type==visual_prefab::component_type))
                    isolated.components.push_back(c);
            validate_scene(isolated);
            scene_=std::move(isolated);
            viewport_->setScene(&scene_);
            updateAll();
        }
        auto const bindings=visual_prefab::links(scene_);
        if(bindings.empty())
            throw std::runtime_error("Scene has no authored visual prefab binding");
        auto owner=std::find_if(scene_.objects.begin(),scene_.objects.end(),
            [&](SceneObject const& object) {
                return object.node.id==bindings.front().first;
            });
        if(owner==scene_.objects.end())
            throw std::runtime_error("Visual prefab owner Entity does not exist");
        auto const index=static_cast<int>(owner-scene_.objects.begin());
        viewport_->setDrawMode(QtEditorViewport::DrawMode::EngineGpu);
        QCoreApplication::processEvents();
        if(!viewport_->gpuActive())
            throw std::runtime_error("Native GPU viewport could not initialize");
        viewport_->setShowCollisions(false);
        try {
            northstarcpp::AssetStore modelAssets;
            modelAssets.mount_project_assets(file_.parent_path(),
                sharedMaterialRoot());
            auto const model=modelAssets.load_drawable(owner->node.asset);
            std::cout<<"Visual Prefab native bounds: "
                <<model.bounds_min[0]<<","<<model.bounds_min[1]<<","
                <<model.bounds_min[2]<<" -> "<<model.bounds_max[0]<<","
                <<model.bounds_max[1]<<","<<model.bounds_max[2]
                <<std::endl;
        }catch(std::exception const& e) {
            std::cerr<<"Visual Prefab optional bounds: "<<e.what()<<std::endl;
        }
        chooseObject(index);
        viewport_->frameSelected();
        if(!viewport_->renderGpuNow())
            throw std::runtime_error("No native GPU frame completed");
        auto const stats=viewport_->gpuAssetSummary();
        std::cout<<"Visual Prefab GPU assets: "<<stats.toStdString()<<std::endl;
        auto const pose=viewport_->camera().pose;
        std::cout<<"Visual Prefab QA camera: "<<pose.position.x<<","<<pose.position.y<<","<<pose.position.z<<" yaw="<<pose.yaw<<" pitch="<<pose.pitch<<std::endl;
        auto const meshCount=QRegularExpression(
            R"(Native meshes: ([0-9]+))").match(stats);
        if(!meshCount.hasMatch() || meshCount.captured(1).toInt()<1)
            throw std::runtime_error(
                ("No real native mesh in GPU frame: "+stats).toStdString());
        auto const& actor=scene_.objects[std::size_t(index)];
        Vector3 const center{actor.position.x,
            actor.position.y+actor.size*.45f,actor.position.z};
        auto const projected=viewport_->camera().project(
            center,float(viewport_->width()),float(viewport_->height()));
        if(!projected)
            throw std::runtime_error("Visual actor is outside the QA camera");
        QPointF const point(projected->x,projected->y);
        if(!viewport_->rect().contains(point.toPoint()))
            throw std::runtime_error("Projected actor is outside the GPU viewport");
        chooseObject(-1);
        auto const global=QPointF(viewport_->mapToGlobal(point.toPoint()));
        QMouseEvent press(QEvent::MouseButtonPress,point,global,
            Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(viewport_,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,point,global,
            Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(viewport_,&release);
        for(int i=0;i<40 && selection_!=index;++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents,20);
            QThread::msleep(15);
        }
        auto const dpi=viewport_->devicePixelRatioF();
        std::cout<<"Visual Prefab GPU: native_meshes="<<meshCount.captured(1).toStdString()
            <<" owner="<<bindings.front().first
            <<" picked="<<selection_
            <<" expected="<<index
            <<" dpi="<<dpi
            <<" logical_size="<<viewport_->width()<<"x"<<viewport_->height()
            <<" status="<<(selection_==index?"PASS":"FAIL")
            <<std::endl;
        auto const capture=qgetenv("NORTHSTARCPP_QA_CAPTURE");
        if(!capture.isEmpty()) {
            QCoreApplication::processEvents();
            auto* screen=windowHandle()?windowHandle()->screen():
                QApplication::primaryScreen();
            bool captured=false;
            if(screen) {
                // Capture the DWM-composited desktop rectangle, not the Qt
                // PrintWindow content of a native child GPU swapchain.
                auto const origin=mapToGlobal(QPoint(0,0));
                auto pixmap=screen->grabWindow(0,origin.x(),origin.y(),
                                               width(),height());
                captured=!pixmap.isNull() &&
                    pixmap.save(QString::fromUtf8(capture),"PNG");
            }
            std::cout<<"Visual Prefab GPU window capture: "
                     <<(captured?"PASS":"FAIL")<<" path="
                     <<capture.constData()<<std::endl;
        }
        viewport_->setDrawMode(QtEditorViewport::DrawMode::Shaded);
        return selection_==index;
    }catch(std::exception const& e) {
        std::cerr<<"Visual Prefab GPU: "<<e.what()<<std::endl;
        return false;
    }
#else
    return false;
#endif
}

bool QtEditorWindow::runGpuSmoke(QString const& root) {
#ifdef _WIN32
    if(!runSmoke(root))return false;
    // Native visual acceptance: a real original YDD, its section YMAT
    // references and decoded YTD textures must reach the GPU renderer.
    bool const nativeFixture=fs::is_regular_file(
        sharedMaterialRoot()/"models/skydome.ydd");
    if(nativeFixture)
        createActor("models/skydome.ydd@skydome_high",{7.f,1.f,7.f});
    viewport_->setDrawMode(QtEditorViewport::DrawMode::EngineGpu);
    QCoreApplication::processEvents();
    QFile diagnostic(QDir(QCoreApplication::applicationDirPath()).filePath(
        "gpu_surface_smoke.log"));
    bool const diagnosticsEnabled=diagnostic.open(
        QIODevice::WriteOnly|QIODevice::Truncate);
    if(diagnosticsEnabled)diagnostic.write(
        ("Active: "+QString::number(viewport_->gpuActive())+
        "\nStatus: "+statusBar()->currentMessage()+"\n").toUtf8());
    diagnostic.flush();
    if(!viewport_->gpuActive()) {
        qWarning() << "GPU QA: initialization failed:"
                   << statusBar()->currentMessage();
        log("smoke: engine.render GPU surface initialization failed");
        return false;
    }
    auto const rendered=viewport_->renderGpuNow();
    auto const stats=viewport_->gpuAssetSummary();
    if(diagnosticsEnabled) {
        diagnostic.write(("Draw: "+QString::number(rendered)+"\nStatus: "+
             statusBar()->currentMessage()+"\nAssets: "+stats+"\n").toUtf8());
        diagnostic.flush();
    }
    bool nativeDrawOk=true;
    if(nativeFixture && rendered) {
        auto const mesh=QRegularExpression(R"(Native meshes: ([0-9]+))").match(stats);
        auto const batches=QRegularExpression(R"(Materials: ([0-9]+))").match(stats);
        auto const textures=QRegularExpression(R"(Textured: ([0-9]+))").match(stats);
        nativeDrawOk=mesh.hasMatch() && mesh.captured(1).toUInt()>0 &&
            batches.hasMatch() && batches.captured(1).toUInt()>0 &&
            textures.hasMatch() && textures.captured(1).toUInt()>0;
        if(!nativeDrawOk)
            log("smoke: YDD/YMAT/YTD did not reach GPU: "+stats);
    }
    viewport_->setDrawMode(QtEditorViewport::DrawMode::Shaded);
    if(!rendered) {
        qWarning() << "GPU QA: frame draw failed:"
                   << statusBar()->currentMessage();
        log("smoke: native GPU draw failed");
    }
    return rendered && nativeDrawOk;
#else
    Q_UNUSED(root);
    log("GPU HWND provider is Windows-only; a portable GPU surface is pending.");
    return false;
#endif
}
}
