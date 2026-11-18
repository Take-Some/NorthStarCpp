#include "qt_editor_window.hpp"
#include "qt_editor_keyboard_shortcuts.hpp"
#include "qt_editor_command_palette.hpp"
#include "qt_editor_theme.hpp"
#include "editor_visual_prefab.hpp"
#include "qt_place_actors_panel.hpp"
#include <northstarcpp/scene_camera_actor.hpp>
#include "../render/shared/render_light_components.hpp"
#include <northstarcpp/collision_authoring.hpp>
#include <northstarcpp/player_character_components.hpp>
#include <northstarcpp/player_blueprint.hpp>
#include "qt_player_blueprint_editor.hpp"
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/collision_mesh.hpp>
#include <northstarcpp/collision_hulls.hpp>
#include "qt_collision_cook_task.hpp"
#include "qt_collision_project_settings.hpp"
#include "qt_collision_manage_channels.hpp"
#include <QInputDialog>
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
// Text asset editing is owned by qt_editor_window_content_docks.cpp.
#include "qt_content_drawer.hpp"
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
#include <QJsonArray>
#include "qt_nui_xml_codec.hpp"
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
#include <QWidgetAction>
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
std::uint64_t QtEditorWindow::selectedPlayerBlueprintPawn() const {
    if(selection_<0 || std::size_t(selection_)>=scene_.objects.size())return 0;
    auto const& selection=scene_.objects[std::size_t(selection_)];
    auto pawn=selection.node.id;
    if(auto const* controller=player_blueprint::find(
            scene_,pawn,player_blueprint::controller_type)) {
        if(auto const binding=player_blueprint::deserialize_controller(controller->payload))
            pawn=binding->first;
    } else if(!player_character::has_pawn(scene_,pawn)) {
        pawn=selection.node.parent;
    }
    auto const layout=player_blueprint::layout(scene_,pawn);
    if(!layout || !layout->camera || !layout->mesh)return 0;
    auto const selected=selection.node.id;
    return selected==layout->pawn || selected==layout->controller ||
           selected==layout->motor || selected==layout->camera ||
           selected==layout->mesh ? pawn : 0;
}
void QtEditorWindow::editSelectedPlayerBlueprint() {
    auto const pawn=selectedPlayerBlueprintPawn();
    if(!pawn) {
        QMessageBox::information(this,"Player Blueprint",
            "Select a Player Blueprint Pawn, Controller, Motor, Camera, or Skeletal Mesh.");
        return;
    }
    auto const selected=scene_.objects[std::size_t(selection_)].node.id;
    try {
        auto changed=player_editor::edit(this,scene_,pawn,file_.parent_path(),selected);
        if(!changed)return;
        editScene([&](Scene& scene){scene=std::move(*changed);},selection_);
        log(QString("Updated Player Blueprint Pawn #%1").arg(pawn));
    } catch(std::exception const& ex) {
        QMessageBox::warning(this,"Player Blueprint",ex.what());
    }
}
void QtEditorWindow::setupUi() {
    // Register editor-authored component contracts with the same global ECS
    // schema registry used to validate serialized Scene V3 records.
    if(!scene_camera::register_schema())
        qWarning("Scene camera component registration failed");
    if(!scene_component_schema("engine.render.light")) {
        auto validLight=[](void*,std::uint32_t version,void const* data,
                           std::size_t length)->bool {
            if((version!=1 && version!=2) || !data || length>512)return false;
            Scene probe;
            probe.version=3;
            probe.components.push_back({0,"engine.render.light",version,
                std::string(static_cast<char const*>(data),length)});
            try {return render::scene_render_lights(probe).size()==1;}
            catch(...) {return false;}
        };
        if(!register_scene_component_type(
            {"engine.render.light",1,2,validLight,nullptr}))
            qWarning("Dynamic scene light component registration failed");
    }
    setStyleSheet(editor_dark_style_sheet());
    viewport_=new QtEditorViewport(this);
    setCentralWidget(viewport_);
    auto* toolbar=addToolBar("Scene & Play");
    toolbar->setObjectName("authoringToolbar");
    toolbar->setAllowedAreas(Qt::TopToolBarArea);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setIconSize(QSize(18,18));
    auto* fileMenu=menuBar()->addMenu("&File");
    auto* editMenu=menuBar()->addMenu("&Edit");
    auto* windowMenu=menuBar()->addMenu("&Window");
    auto* viewMenu=menuBar()->addMenu("&View");
    viewMenu->addAction("Native UI Preview",this,[this] {
        auto* dock=findChild<QDockWidget*>("nativeUiPreviewDock");
        if(!dock) {
            try {
                dock=new QDockWidget("Native UI Preview",this);
                dock->setObjectName("nativeUiPreviewDock");
                dock->setWidget(new NativeUIPreview(dock));
                addDockWidget(Qt::RightDockWidgetArea,dock);
            } catch(const std::exception& exception) {
                if(dock && !dock->widget())dock->deleteLater();
                statusBar()->showMessage(
                    QString("Native UI Preview: %1").arg(exception.what()),10000);
                return;
            }
        }
        dock->show();
        dock->raise();
    });
    auto* assetMenu=menuBar()->addMenu("&Content");
    assetMenu->addAction("Scene Sky...",this,[this] {browser_->editSceneSky();});
    auto* toolsMenu=menuBar()->addMenu("&Tools");
    auto* gameMenu=menuBar()->addMenu("&Play");
    auto* newScene=fileMenu->addAction("&New Scene",QKeySequence::New,[this]{
        if(!confirmDiscard())return;
        if(!file_.empty()) {
            QSettings settings("KaylasSystems","NorthStarQtEditor");
            settings.setValue(sceneCameraKey(file_)+"/LastCamera",
                              cameraValues(viewport_->camera()));
        }
        file_.clear();scene_=make_startup_scene();
        editor_tools::promote_to_v3(scene_);
        history_.clear();selection_=-1;dirty_=false;
        browser_->setProjectScene({});
        viewport_->setAssetSources({});
        updateAll();
    });
    auto* open=fileMenu->addAction("&Open Scene...",QKeySequence::Open,[this] {
        if(!confirmDiscard())return;
        QFileDialog dialog(this,"Open NorthStar Scene",qpath(file_.parent_path()));
        dialog.setNameFilter("NorthStar Scenes (*.nscene *.nscn);;All Files (*)");
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setOption(QFileDialog::DontUseNativeDialog,true);
        if(dialog.exec()==QDialog::Accepted && !dialog.selectedFiles().isEmpty())
            openScene(dialog.selectedFiles().front());
    });
    saveAction_=fileMenu->addAction("&Save Scene",QKeySequence::Save,[this]{
        saveScene();
    });
    fileMenu->addAction("Save &As...",QKeySequence::SaveAs,[this]{
        saveScene("*save-as*");
    });
    fileMenu->addSeparator();
    fileMenu->addAction("Capture Project Thumbnail",this,[this] {
        QTimer::singleShot(250,this,[this] {
            QString message;
            if(!captureProjectThumbnail(&message))
                QMessageBox::warning(this,"Project Thumbnail",message);
            else statusBar()->showMessage(message,8000);
        });
    });
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit",QKeySequence::Quit,this,&QWidget::close);

    undoAction_=editMenu->addAction("&Undo",QKeySequence::Undo,this,&QtEditorWindow::undo);
    redoAction_=editMenu->addAction("&Redo",QKeySequence::Redo,this,&QtEditorWindow::redo);
    editMenu->addSeparator();
    auto* addPrimitive=editMenu->addAction("Add &Primitive",QKeySequence(Qt::CTRL|Qt::Key_Insert),
        [this]{createActor({},{0,0,0});});
    // The Editor and Player both resolve these geometry records via ABI.
    auto* primitives=editMenu->addMenu("Add NorthStar Geometry");
    auto addGeometry=[this,primitives](char const* label) {
        primitives->addAction(QString::fromUtf8(label),[this,label] {
            placeActorById("geometry."+QString::fromUtf8(label).toLower(),{0,0,0});
        });
    };
    addGeometry("Cube");
    addGeometry("Cylinder");
    addGeometry("Ellipsoid");
    addGeometry("Ring");
    addGeometry("Torus");
    auto* addPlayerBlueprint=editMenu->addAction(
        "Add Player Blueprint (Controller + Pawn + Camera + Mesh)",this,[this] {
        placeActorById("gameplay.player_blueprint",{3,0,0});
    });
    addPlayerBlueprint->setObjectName("addPlayerBlueprintAction");
    auto* editPlayerBlueprint=editMenu->addAction(
        "Edit Player Blueprint...",this,&QtEditorWindow::editSelectedPlayerBlueprint);
    editPlayerBlueprint->setObjectName("editPlayerBlueprintAction");
    auto* addCharacter=editMenu->addAction("Add Character (Pawn)",this,[this] {
        placeActorById("gameplay.character",{3,0,0});
    });
    addCharacter->setObjectName("addCharacterPawnAction");
    auto* configureCharacter=editMenu->addAction(
        "Configure Character Components",this,[this] {
        int target=selection_;
        if(target<0 || std::size_t(target)>=scene_.objects.size())
            for(std::size_t i=0;i<scene_.objects.size();++i)
                if(scene_pawn::is_local(scene_,scene_.objects[i])) {
                    target=static_cast<int>(i);
                    break;
                }
        if(target<0 || std::size_t(target)>=scene_.objects.size()) {
            statusBar()->showMessage(
                "Select a Player or another actor to configure as a Character.",6000);
            return;
        }
        auto const id=scene_.objects[std::size_t(target)].node.id;
        editScene([target](Scene& scene) {
            editor_tools::promote_to_v3(scene);
            auto const& actor=scene.objects[std::size_t(target)];
            player_character::add_character_components(
                scene,actor.node.id,!scene_pawn::local_index(scene).has_value() ||
                    scene_pawn::is_local(scene,actor));
        },target);
        log(QString("Character components configured for entity %1").arg(id));
    });
    configureCharacter->setObjectName("configureCharacterComponentsAction");
    // Placement is exposed only through the searchable Add Actor palette.
    // Keep the QActions alive: the palette reuses them for shortcuts and undo.
    editMenu->removeAction(addPrimitive);
    editMenu->removeAction(primitives->menuAction());
    editMenu->removeAction(addPlayerBlueprint);
    editMenu->removeAction(addCharacter);
    editMenu->removeAction(editPlayerBlueprint);
    editMenu->removeAction(configureCharacter);
    auto* characterTools=toolsMenu->addMenu("Character");
    characterTools->addAction(editPlayerBlueprint);
    characterTools->addAction(configureCharacter);
    editMenu->addAction("&Duplicate Object",QKeySequence(Qt::CTRL|Qt::Key_D),
                        this,&QtEditorWindow::duplicateSelected)->setObjectName("duplicateActorsAction");
    editMenu->addAction("&Rename Actor",QKeySequence(Qt::Key_F2),
                        this,&QtEditorWindow::renameSelected)->setObjectName("renameActorAction");
    editMenu->addAction("Delete Object",QKeySequence::Delete,
                        this,&QtEditorWindow::deleteSelected)->setObjectName("deleteActorsAction");
    // Legacy .material.json creation is intentionally not exposed in the
    // primary menu; native YMAT authoring is opened from Content Browser.
    assetMenu->addAction("New Material &Instance...",[this] {
        if(file_.empty()) {
            QMessageBox::information(this,"Project Required",
                "Save or open a scene to create a material instance.");
            return;
        }
        auto const root=native(folderRoot(file_));
        QStringList parents;
        for(auto const& ref:material_authoring::list_materials(root)) {
            if(ref.endsWith(".ymat",Qt::CaseInsensitive) ||
               material_authoring::is_material_instance(ref))
                parents.push_back(ref);
        }
        if(parents.isEmpty()) {
            QMessageBox::information(this,"Material Instance",
                "Create a project material source before creating an instance.");
            return;
        }
        bool accepted=false;
        QString parent=QInputDialog::getItem(this,"New Material Instance",
            "Parent material:",parents,0,false,&accepted);
        if(!accepted || parent.isEmpty())return;
        QString const name=QInputDialog::getText(this,"New Material Instance",
            "Instance name (letters, numbers, _ or -):",
            QLineEdit::Normal,"NewInstance",&accepted);
        if(!accepted)return;
        try {
            QString ref=material_authoring::create_material_instance(root,parent,name);
            browser_->refresh();
            refreshMaterialChoices(true);
            log("Created Material Instance: "+ref+" (inherits "+parent+")");
            openMaterialEditor(ref);
        }catch(std::exception const& e) {
            QMessageBox::warning(this,"Material Instance",from8(e.what()));
        }
    });
    assetMenu->addAction("&Material Editor...",[this] {
        auto items=material_authoring::list_materials(native(folderRoot(file_)));
        if(items.isEmpty()) {
            QMessageBox::information(this,"Material Editor",
                "No materials found. Create a material or drag a texture onto an actor.");
            return;
        }
        QString selected=materialPicker_ && selection_>=0 ?
            materialPicker_->currentData().toString():QString{};
        bool accepted=false;
        if(selected.isEmpty() || !items.contains(selected))
            selected=QInputDialog::getItem(this,"Material Editor",
                "Choose a material",items,0,false,&accepted);
        else accepted=true;
        if(accepted && !selected.isEmpty())openMaterialEditor(selected);
    });
    auto* newUiAction=assetMenu->addAction("New UI Document...",[this] {
        if(file_.empty()) {
            QMessageBox::information(this,"UI Document",
                "Open or save a scene before creating a project UI asset.");
            return;
        }
        bool ok=false;
        auto name=QInputDialog::getText(this,"New UI Document",
            "Asset name:",QLineEdit::Normal,"new_screen",&ok).trimmed();
        static const QRegularExpression safe("^[A-Za-z][A-Za-z0-9_-]{0,63}$");
        if(!ok)return;
        if(!safe.match(name).hasMatch()) {
            QMessageBox::warning(this,"UI Document",
                "Use a unique name containing letters, digits, _ or -.");
            return;
        }
        QDir dir(folderRoot(file_));
        if(!dir.mkpath("ui")) {
            QMessageBox::warning(this,"UI Document",
                "Cannot create the project UI directory.");
            return;
        }
        auto const physical=dir.filePath("ui/"+name+".nui");
        if(QFileInfo::exists(physical)) {
            QMessageBox::warning(this,"UI Document","This UI asset already exists.");
            return;
        }
        QJsonObject doc{
            {"schema","northstar.ui/2"},{"target","screen"},
            {"tokens",QJsonObject{}},{"styles",QJsonObject{}},
            {"layer",name},
            {"nodes",QJsonArray{
                QJsonObject{{"id","root"},{"parent",""},{"type","panel"},
                    {"props",QJsonObject{{"x",40},{"y",40},{"width",460},
                                         {"height",240},{"padding",20},
                                         {"background","#142030ee"}}}},
                QJsonObject{{"id","title"},{"parent","root"},{"type","text"},
                    {"props",QJsonObject{{"text","NEW UI SCREEN"},
                                         {"font_scale",2},{"height",46},
                                         {"color","#ffffffff"}}}}
            }}
        };
        QSaveFile out(physical);
        auto const data=nui_qt::serialize(doc);
        if(!out.open(QIODevice::WriteOnly) ||
           out.write(data)!=data.size() || !out.commit()) {
            QMessageBox::warning(this,"UI Document","Could not save new UI asset.");
            return;
        }
        browser_->refresh();
        openUiAsset(physical,false);
    });
    newUiAction->setObjectName("createUiDocumentAction");
    auto* inspectSkeleton=toolsMenu->addAction("Inspect Skeleton Rig...",[this] {
        const auto file=QFileDialog::getOpenFileName(this,
            "Open NorthStar Skeleton",{},"Skeleton Rig (*.skeleton.json *.json);;All Files (*)");
        if(file.isEmpty())return;
        try {
            QFile input(file);
            if(!input.open(QIODevice::ReadOnly))
                throw std::runtime_error("Cannot read selected skeleton resource");
            if(input.size()>1024*1024)
                throw std::invalid_argument("Skeleton exceeds 1 MiB safety limit");
            auto rig=northstarcpp::parse_skeleton_rig(
                input.readAll().toStdString());
            auto pose=rig.skinning_palette({},0.f);
            auto packed=northstarcpp::pack_skinning_palette(pose);
            QDialog dialog(this);
            dialog.setWindowTitle("NorthStar Skeleton Inspector");
            dialog.resize(640,480);
            auto* layout=new QVBoxLayout(&dialog);
            layout->addWidget(new QLabel(QString("%1 bones | %2 GPU palette floats | "
                "CPU hierarchy and bind pose validated").arg(rig.bone_count())
                .arg(packed.size()),&dialog));
            auto* tree=new QTreeWidget(&dialog);
            tree->setObjectName("skeletonBoneTree");
            tree->setHeaderLabels({"Bone","Index","Parent","Bind Translation","Bind Scale"});
            tree->setAlternatingRowColors(true);
            tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
            std::vector<QTreeWidgetItem*> items;
            items.reserve(rig.bone_count());
            for(std::size_t i=0;i<rig.bone_count();++i) {
                const auto& bone=rig.bones()[i];
                auto format=[](northstarcpp::Vector3 v) {
                    return QString("(%1, %2, %3)")
                        .arg(v.x,0,'f',3).arg(v.y,0,'f',3).arg(v.z,0,'f',3);
                };
                auto* parent=bone.parent>=0?items[std::size_t(bone.parent)]:nullptr;
                auto* entry=parent?
                    new QTreeWidgetItem(parent):
                    new QTreeWidgetItem(tree);
                entry->setText(0,QString::fromStdString(bone.name));
                entry->setText(1,QString::number(i));
                entry->setText(2,bone.parent<0?"root":QString::number(bone.parent));
                entry->setText(3,format(bone.bind.translation));
                entry->setText(4,format(bone.bind.scale));
                items.push_back(entry);
            }
            tree->expandAll();
            layout->addWidget(tree);
            auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
            QObject::connect(buttons,&QDialogButtonBox::rejected,
                             &dialog,&QDialog::reject);
            layout->addWidget(buttons);
            dialog.exec();
            log(QString("Skeleton rig validated: %1 bones, %2 palette floats: %3")
                .arg(rig.bone_count()).arg(packed.size()).arg(file));
        } catch(std::exception const& error) {
            QMessageBox::warning(this,"Skeleton validation failed",
                QString::fromUtf8(error.what()));
            log(QString("Skeleton validation failed: %1: %2")
                .arg(file,QString::fromUtf8(error.what())));
        }
    });
    inspectSkeleton->setObjectName("inspectSkeletonRigAction");

    assetMenu->addAction("&Refresh Content",QKeySequence(Qt::Key_F5),[this]{
        browser_->refresh();
        updateDependencies();
    });
    playAction_=gameMenu->addAction("Play In Editor",QKeySequence(Qt::Key_F6),
                                    this,&QtEditorWindow::playGame);
    playAction_->setObjectName("playGameAction");
    stopAction_=gameMenu->addAction("Stop Game",
                                    QKeySequence(Qt::SHIFT|Qt::Key_F6),
                                    this,&QtEditorWindow::stopGame);
    stopAction_->setObjectName("stopGameAction");
    stopAction_->setEnabled(false);
    auto* standalone=gameMenu->addAction("Launch Standalone Player",
                                        this,&QtEditorWindow::playGameStandalone);
    standalone->setObjectName("launchStandalonePlayerAction");
    exploreModeAction_=gameMenu->addAction("First Person Exploration");
    exploreModeAction_->setCheckable(true);
    exploreModeAction_->setObjectName("exploreModeAction");
    fpsModeAction_=gameMenu->addAction("FPS Arena Game Mode");
    fpsModeAction_->setCheckable(true);
    fpsModeAction_->setObjectName("fpsModeAction");
    connect(exploreModeAction_,&QAction::toggled,fpsModeAction_,
            [this](bool enabled){if(enabled)fpsModeAction_->setChecked(false);});
    connect(fpsModeAction_,&QAction::toggled,exploreModeAction_,
            [this](bool enabled){if(enabled)exploreModeAction_->setChecked(false);});
    gameMenu->addSeparator();
    auto* configure=toolsMenu->addAction("Player Executable...",
                                        this,&QtEditorWindow::choosePlayerExecutable);
    configure->setObjectName("configurePlayerAction");
    // One placement palette is used by both the main toolbar and Outliner.
    // Reuse the underlying QAction instances, so every entry travels through
    // the existing scene transaction, selection, undo and serialization path.
    auto makePlaceActorMenu=[this,primitives,addPlayerBlueprint,addCharacter](
        QWidget* owner)->QMenu* {
        auto* menu=new QMenu(owner);
        menu->setObjectName("placeActorMenu");
        auto* searchBox=new QWidget(menu);
        searchBox->setObjectName("actorPlacementSearchBox");
        auto* searchLayout=new QHBoxLayout(searchBox);
        searchLayout->setContentsMargins(7,4,7,5);
        auto* search=new QLineEdit(searchBox);
        search->setObjectName("actorPlacementSearch");
        search->setPlaceholderText("Search placeable actors...");
        search->setClearButtonEnabled(true);
        searchLayout->addWidget(search);
        auto* searchWidgetAction=new QWidgetAction(menu);
        searchWidgetAction->setDefaultWidget(searchBox);
        menu->addAction(searchWidgetAction);

        auto* shapes=menu->addMenu("Shapes / Geometry");
        shapes->setObjectName("placeActorShapes");
        for(auto* action:primitives->actions())shapes->addAction(action);
        auto* gameplay=menu->addMenu("Characters / Gameplay");
        gameplay->setObjectName("placeActorGameplay");
        gameplay->addAction(addPlayerBlueprint);
        gameplay->addAction(addCharacter);
        auto* cameras=menu->addMenu("Cameras");
        auto* cameraAction=cameras->addAction("Camera Actor",[this] {
            placeActorById("camera.free",{0,0,0});
        });
        auto* lights=menu->addMenu("Lights");
        auto* pointLight=lights->addAction("Point Light",[this] {
            placeActorById("light.point",{0,0,0});
        });
        auto* spotLight=lights->addAction("Spot Light",[this] {
            placeActorById("light.spot",{0,0,0});
        });
        lights->addAction("Directional Light",[this] {
            placeActorById("light.directional",{0,0,0});
        });
        lights->addAction("Sky Light",[this] {
            placeActorById("light.sky",{0,0,0});
        });
        auto* components=menu->addMenu("Provider Components");
        QVector<std::pair<QString,QAction*>> componentActions;
        for(auto const& entry:availablePlaceActors())
            if(entry.category=="Components") {
                auto* action=components->addAction(entry.label,[this,id=entry.id] {
                    placeActorById(id,{0,0,0});
                });
                componentActions.push_back({entry.description,action});
            }
        components->menuAction()->setVisible(!componentActions.isEmpty());
        menu->addSeparator();
        auto* browse=menu->addAction("Browse Project / Engine Assets...");
        browse->setObjectName("browsePlaceableAssetsAction");
        browse->setToolTip(
            "Use the Content Browser to place native models and prefabs");
        connect(browse,&QAction::triggered,this,
                [this] {showContentBrowser();});

        // Search results are menu-owned proxy actions. Hiding a shared
        // QAction would also hide it in Edit and the other placement menu.
        auto* resultsDivider=menu->addSeparator();
        resultsDivider->setVisible(false);
        auto* noMatches=menu->addAction("No matching actor types");
        noMatches->setEnabled(false);
        noMatches->setVisible(false);
        std::vector<std::pair<QString,QAction*>> entries;
        auto addSearchAction=[menu,&entries](QAction* target,
                                             QString const& keywords) {
            auto* result=menu->addAction(target->text());
            result->setVisible(false);
            QObject::connect(result,&QAction::triggered,target,
                             &QAction::trigger);
            entries.emplace_back(target->text()+" "+keywords,result);
        };
        for(auto* action:primitives->actions())
            if(!action->isSeparator())
                addSearchAction(action,"shape geometry primitive mesh");
        addSearchAction(addPlayerBlueprint,
                        "player controller pawn camera skeletal mesh animation");
        addSearchAction(addCharacter,"character pawn motor capsule");
        addSearchAction(cameraAction,"camera cinematic fov priority");
        addSearchAction(pointLight,"light omnidirectional dynamic");
        addSearchAction(spotLight,"light cone spot dynamic");
        for(auto const& [description,action]:componentActions)
            addSearchAction(action,"provider plugin ecs "+description);
        connect(search,&QLineEdit::textChanged,menu,
            [shapes,gameplay,cameras,lights,components,browse,
             resultsDivider,noMatches,entries,hasComponents=!componentActions.isEmpty()](
                QString const& query) {
            auto const needle=query.trimmed();
            bool const filtering=!needle.isEmpty();
            shapes->menuAction()->setVisible(!filtering);
            gameplay->menuAction()->setVisible(!filtering);
            cameras->menuAction()->setVisible(!filtering);
            lights->menuAction()->setVisible(!filtering);
            components->menuAction()->setVisible(!filtering && hasComponents);
            browse->setVisible(!filtering);
            resultsDivider->setVisible(filtering);
            int hits=0;
            for(auto const& [keywords,action]:entries) {
                bool const match=filtering &&
                    keywords.contains(needle,Qt::CaseInsensitive);
                action->setVisible(match);
                if(match)++hits;
            }
            noMatches->setVisible(filtering && hits==0);
        });
        connect(menu,&QMenu::aboutToShow,search,
                [search] {search->clear();});
        return menu;
    };
    auto* sceneLabel=new QLabel("SCENE",toolbar);
    sceneLabel->setObjectName("toolbarSectionLabel");
    toolbar->addWidget(sceneLabel);
    toolbar->addAction(saveAction_);
    toolbar->addAction(undoAction_);
    toolbar->addAction(redoAction_);
    toolbar->addSeparator();
    auto* createButton=new QToolButton(toolbar);
    createButton->setObjectName("createActorButton");
    createButton->setText(" + Add Actor");
    createButton->setToolTip(
        "Search and place a procedural shape or gameplay actor");
    createButton->setPopupMode(QToolButton::InstantPopup);
    createButton->setMenu(makePlaceActorMenu(createButton));
    toolbar->addWidget(createButton);
    // The full Place Actors panel is available under Window, not duplicated
    // on the already compact main toolbar.
    auto* contentButton=new QToolButton(toolbar);
    contentButton->setObjectName("toolbarContentButton");
    contentButton->setText("Content");
    contentButton->setToolTip("Open the docked Content Browser");
    connect(contentButton,&QToolButton::clicked,this,
            [this]{showContentBrowser();});
    toolbar->addWidget(contentButton);
    auto* searchButton=new QToolButton(toolbar);
    searchButton->setObjectName("toolbarFindCommand");
    searchButton->setText("Search Actions");
    searchButton->setToolTip("Find any editor command (Ctrl+Shift+P)");
    connect(searchButton,&QToolButton::clicked,this,[this] {
        if(auto* action=findChild<QAction*>("editorCommandPaletteAction"))
            action->trigger();
    });
    toolbar->addWidget(searchButton);

    toolbar->addSeparator();
    auto* runLabel=new QLabel("RUN",toolbar);
    runLabel->setObjectName("toolbarSectionLabel");
    toolbar->addWidget(runLabel);
    // Compact PIE control strip: use the real 64x64 engine resources rather
    // than text labels or platform-generated glyphs.
    // Actions remain the single source of truth for play/stop keyboard shortcuts
    // and process-start/exit enable-state updates.
    auto iconFor=[](char const* normal,char const* disabled=nullptr) {
        QIcon icon;
        auto path=[](char const* name) {
            return QStringLiteral(":/northstar/play/")+QString::fromLatin1(name);
        };
        icon.addFile(path(normal),QSize(64,64),QIcon::Normal);
        if(disabled)
            icon.addFile(path(disabled),QSize(64,64),QIcon::Disabled);
        return icon;
    };
    playAction_->setIcon(iconFor("play.png","play_disabled_64.png"));
    stopAction_->setIcon(iconFor("stop-active.png","stop-disabled.png"));
    playAction_->setToolTip("Play In Editor (F6)");
    stopAction_->setToolTip("Stop Game (Shift+F6)");
    // Native Player handles pause/resume/eject/possess on its window thread.
    // These actions are enabled only for a running, attached PIE session.
    // No OS process suspension: use the Player's simulation-control IPC.
    // Eject retains the embedded HWND and hands editor focus back safely.
    pauseAction_=gameMenu->addAction("Pause Game",this,&QtEditorWindow::pauseGame);
    resumeAction_=gameMenu->addAction("Resume Game",this,&QtEditorWindow::resumeGame);
    stepAction_=gameMenu->addAction("Step One Frame",this,&QtEditorWindow::stepGame);
    ejectAction_=gameMenu->addAction("Eject Game",this,&QtEditorWindow::ejectGame);
    auto* pauseAction=pauseAction_;
    auto* resumeAction=resumeAction_;
    auto* stepAction=stepAction_;
    auto* ejectAction=ejectAction_;
    pauseAction->setObjectName("pauseGameAction");
    resumeAction->setObjectName("resumeGameAction");
    stepAction->setObjectName("stepGameAction");
    ejectAction->setObjectName("ejectGameAction");
    pauseAction->setIcon(iconFor("pause-active.png"));
    resumeAction->setIcon(iconFor("resume-active.png","resume-disabled.png"));
    // Editor-painted single-frame advance: triangle with a vertical stop bar.
    QPixmap stepPixels(64,64);
    stepPixels.fill(Qt::transparent);
    {
        QPainter painter(&stepPixels);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#dceaf5"));
        painter.drawPolygon(QPolygonF{
            QPointF(13,14),QPointF(13,50),QPointF(43,32)});
        painter.drawRoundedRect(QRectF(48,14,5,36),1,1);
    }
    stepAction->setIcon(QIcon(stepPixels));
    ejectAction->setIcon(iconFor("eject-active.png","eject-disabled.png"));
    pauseAction->setToolTip("Pause simulation (F7)");
    pauseAction->setShortcut(QKeySequence(Qt::Key_F7));
    resumeAction->setToolTip("Resume simulation (Shift+F7)");
    resumeAction->setShortcut(QKeySequence(Qt::SHIFT|Qt::Key_F7));
    ejectAction->setToolTip("Eject to Editor; keep the game running (F8)");
    ejectAction->setShortcut(QKeySequence(Qt::Key_F8));
    stepAction->setShortcut(QKeySequence(Qt::Key_F9));
    stepAction->setToolTip("Advance a single 1/60 s simulation frame (F9)");
    pauseAction->setEnabled(false);
    resumeAction->setEnabled(false);
    stepAction->setEnabled(false);
    ejectAction->setEnabled(false);
    auto* pieControls=new QWidget(toolbar);
    pieControls->setObjectName("pieControlGroup");
    auto* controlsLayout=new QHBoxLayout(pieControls);
    controlsLayout->setContentsMargins(3,2,3,2);
    controlsLayout->setSpacing(2);
    const std::array<std::pair<QAction*,char const*>,6> controls{{
        {playAction_,"piePlayButton"},
        {pauseAction,"piePauseButton"},
        {resumeAction,"pieResumeButton"},
        {stepAction,"pieStepButton"},
        {stopAction_,"pieStopButton"},
        {ejectAction,"pieEjectButton"}
    }};
    for(auto const& [action,name]:controls) {
        auto* button=new QToolButton(pieControls);
        button->setObjectName(QString::fromLatin1(name));
        button->setProperty("pieControl",true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        button->setFixedSize(36,36);
        button->setIconSize(QSize(26,26));
        button->setDefaultAction(action);
        controlsLayout->addWidget(button);
    }
    toolbar->addWidget(pieControls);
    // A UE-style split between transport controls and launch configuration.
    // The Play command remains the same QAction (F6) and mode selection
    // updates the real runtime configuration, not a second PIE state machine.
    auto* playModes=new QToolButton(toolbar);
    playModes->setObjectName("playModeButton");
    playModes->setText("Play Options");
    playModes->setToolTip("Choose embedded or standalone launch and game mode");
    playModes->setPopupMode(QToolButton::InstantPopup);
    auto* playModesMenu=new QMenu(playModes);
    playModesMenu->addAction(playAction_);
    playModesMenu->addAction(standalone);
    playModesMenu->addSeparator();
    playModesMenu->addAction(exploreModeAction_);
    playModesMenu->addAction(fpsModeAction_);
    playModes->setMenu(playModesMenu);
    toolbar->addWidget(playModes);
    // Stop remains in the Game menu and is owned by the PIE icon strip.
    playerProcess_=new QProcess(this);
    playerProcess_->setProcessChannelMode(QProcess::MergedChannels);
    connect(playerProcess_,&QProcess::started,this,[this] {
        playAction_->setEnabled(false);
        stopAction_->setEnabled(true);
        if(playSurface_)
            playSurface_->setPlayerProcessId(quint64(playerProcess_->processId()));
        updatePieControls();
        log("Game player started: "+playerProcess_->program());
    });
    connect(playerProcess_,&QProcess::readyReadStandardOutput,this,[this] {
        auto output=QString::fromUtf8(playerProcess_->readAllStandardOutput());
        if(!output.trimmed().isEmpty()) {
            if(output.size()>2500)output=output.right(2500);
            log("[Player] "+output.trimmed());
            if(qEnvironmentVariableIsSet("NORTHSTAR_PIE_TRACE"))
                std::cerr<<"[Player] "<<output.toStdString()<<std::endl;
        }
    });
    connect(playerProcess_,&QProcess::errorOccurred,this,
            [this](QProcess::ProcessError error) {
        if(error==QProcess::FailedToStart || error==QProcess::Crashed)
            log("Game player error: "+playerProcess_->errorString());
        if(error==QProcess::FailedToStart) {
            playAction_->setEnabled(true);
            stopAction_->setEnabled(false);
            endPlayOverlay();
        }
    });
    connect(playerProcess_,QOverload<int,QProcess::ExitStatus>::of(
            &QProcess::finished),this,
            [this](int exitCode,QProcess::ExitStatus status) {
        playAction_->setEnabled(true);
        stopAction_->setEnabled(false);
        endPlayOverlay();
        log(QString("Game player finished (%1, exit %2)")
            .arg(status==QProcess::NormalExit?"normal":"crashed")
            .arg(exitCode));
    });

    // Viewport-only controls live in their own row; the PIE strip stays
    // visible independently of window width and viewport settings.
    addToolBarBreak(Qt::TopToolBarArea);
    auto* viewportToolbar=addToolBar("Viewport");
    viewportToolbar->setObjectName("viewportToolbar");
    viewportToolbar->setAllowedAreas(Qt::TopToolBarArea);
    viewportToolbar->setMovable(false);
    viewportToolbar->setFloatable(false);
    auto* viewportLabel=new QLabel("VIEWPORT",viewportToolbar);
    viewportLabel->setObjectName("toolbarSectionLabel");
    viewportToolbar->addWidget(viewportLabel);
    auto* modeMove=viewportToolbar->addAction("Move");
    modeMove->setToolTip("Translate selected actor (Ctrl+1)");
    auto* modeRotate=viewportToolbar->addAction("Rotate");
    modeRotate->setToolTip("Rotate selected actor (Ctrl+2)");
    auto* modeScale=viewportToolbar->addAction("Scale");
    modeScale->setToolTip("Scale selected actor (Ctrl+3)");
    // Plain W/E/R keys belong to viewport fly navigation while RMB is held.
    // Explicit Ctrl+1/2/3 avoids shortcut interception by QMainWindow.
    modeMove->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_1));
    modeRotate->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_2));
    modeScale->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_3));
    modeMove->setCheckable(true);
    modeRotate->setCheckable(true);
    modeScale->setCheckable(true);
    modeMove->setChecked(true);
    auto setMode=[this,modeMove,modeRotate,modeScale](int mode) {
        for(auto* action:{modeMove,modeRotate,modeScale})action->setChecked(false);
        auto* current=mode==0?modeMove:mode==1?modeRotate:modeScale;
        current->setChecked(true);
        viewport_->setGizmoMode(mode);
    };
    connect(modeMove,&QAction::triggered,this,[=]{setMode(0);});
    connect(modeRotate,&QAction::triggered,this,[=]{setMode(1);});
    connect(modeScale,&QAction::triggered,this,[=]{setMode(2);});
    connect(viewport_,&QtEditorViewport::gizmoModeRequested,
            this,[setMode](int mode){setMode(mode);});
    auto* snap=new QCheckBox("Snap",viewportToolbar);
    snap->setObjectName("viewportSnapToggle");
    snap->setToolTip("Snap actor transforms to the configured grid increments");
    snap->setChecked(gizmoOptions_.snap);
    connect(snap,&QCheckBox::toggled,this,[this](bool checked){
        gizmoOptions_.snap=checked;
    });
    viewportToolbar->addWidget(snap);
    viewportToolbar->addSeparator();
    viewportToolbar->addAction("Focus",QKeySequence(Qt::Key_F),viewport_,
                       &QtEditorViewport::frameSelected);
    viewportToolbar->addAction("Frame All",QKeySequence(Qt::Key_Home),viewport_,
                       &QtEditorViewport::frameAll);
    viewportToolbar->addSeparator();

    auto* viewModes=new QComboBox(viewportToolbar);
    viewModes->setObjectName("viewportDrawMode");
    viewModes->addItems({"Shaded (Proxy only)","Wireframe (Proxy)",
                         "Shaded + Wireframe (Proxy)",
                         "Engine GPU — Native Models & Materials"});
    viewModes->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    viewModes->setMinimumContentsLength(23);
    viewModes->setMaximumWidth(280);
    viewModes->setToolTip(
        "Engine GPU uses the native YDD/YDR geometry, YMAT/YTD materials "
        "and assigned actor material components. Proxy views show cubes.");
    viewportToolbar->addWidget(new QLabel(" View: ",viewportToolbar));
    viewportToolbar->addWidget(viewModes);
    connect(viewModes,QOverload<int>::of(&QComboBox::currentIndexChanged),
            viewport_,[this](int mode) {
        viewport_->setDrawMode(static_cast<QtEditorViewport::DrawMode>(mode));
    });
    connect(viewport_,&QtEditorViewport::gpuUnavailable,this,
            [this,viewModes](QString error) {
        if(viewModes->currentIndex()==3)
            viewModes->setCurrentIndex(0);
        log("GPU Viewport unavailable; Qt proxy restored: "+error);
    });
    auto* gridToggle=new QCheckBox("Grid",viewportToolbar);
    gridToggle->setObjectName("viewportGridToggle");
    gridToggle->setChecked(true);
    viewportToolbar->addWidget(gridToggle);
    connect(gridToggle,&QCheckBox::toggled,viewport_,
            &QtEditorViewport::setShowGrid);
    auto* collisionWire=new QCheckBox("Colliders",viewportToolbar);
    collisionWire->setObjectName("viewportCollisionWireframe");
    collisionWire->setToolTip("Show authored box, sphere and capsule colliders. "
                              "Amber: selected, teal: enabled, red: disabled.");
    viewportToolbar->addWidget(collisionWire);
    connect(collisionWire,&QCheckBox::toggled,viewport_,
            &QtEditorViewport::setShowCollisions);
    auto* collisionAction=viewMenu->addAction("Collision Wireframe");
    collisionAction->setObjectName("viewCollisionWireframe");
    collisionAction->setCheckable(true);
    connect(collisionAction,&QAction::toggled,collisionWire,
            &QCheckBox::setChecked);
    connect(collisionWire,&QCheckBox::toggled,collisionAction,
            &QAction::setChecked);
    auto* speed=new QDoubleSpinBox(viewportToolbar);
    speed->setObjectName("viewportFlySpeed");
    speed->setToolTip("Fly movement speed; RMB + wheel also adjusts this");
    speed->setRange(.1,2000.);
    speed->setDecimals(1);
    speed->setSuffix(" m/s");
    speed->setValue(viewport_->flySpeed());
    speed->setSingleStep(2.);
    viewportToolbar->addWidget(new QLabel(" Fly speed: ",viewportToolbar));
    viewportToolbar->addWidget(speed);
    connect(speed,QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            viewport_,[this](double value) {
        viewport_->setFlySpeed(float(value));
    });
    connect(viewport_,&QtEditorViewport::flySpeedChanged,speed,
            [speed](float value) {
        QSignalBlocker block(speed);
        speed->setValue(double(value));
    });
    viewMenu->addSeparator();
    auto* renderMenu=viewMenu->addMenu("Viewport Rendering");
    auto* shaded=renderMenu->addAction("Shaded");
    auto* wire=renderMenu->addAction("Wireframe");
    auto* shadedWire=renderMenu->addAction("Shaded + Wireframe");
    auto* engineGpu=renderMenu->addAction("Engine GPU");
    connect(shaded,&QAction::triggered,viewModes,[viewModes]{
        viewModes->setCurrentIndex(0);
    });
    connect(wire,&QAction::triggered,viewModes,[viewModes]{
        viewModes->setCurrentIndex(1);
    });
    connect(shadedWire,&QAction::triggered,viewModes,[viewModes]{
        viewModes->setCurrentIndex(2);
    });
    connect(engineGpu,&QAction::triggered,viewModes,[viewModes]{
        viewModes->setCurrentIndex(3);
    });
    viewMenu->addSeparator();
    auto* bookmarkMenu=viewMenu->addMenu("Camera Bookmarks");
    for(int bookmark=1;bookmark<=3;++bookmark) {
        auto* save=bookmarkMenu->addAction(
            QString("Store Bookmark %1").arg(bookmark));
        save->setShortcut(QKeySequence(Qt::CTRL|Qt::ALT|
                                      (Qt::Key_0+bookmark)));
        connect(save,&QAction::triggered,this,[this,bookmark] {
            QSettings settings("KaylasSystems","NorthStarQtEditor");
            settings.setValue(sceneCameraKey(file_)+
                QString("/Bookmark%1").arg(bookmark),
                cameraValues(viewport_->camera()));
            statusBar()->showMessage(
                QString("Saved camera bookmark %1").arg(bookmark),3500);
        });
        auto* recall=bookmarkMenu->addAction(
            QString("Go To Bookmark %1").arg(bookmark));
        recall->setShortcut(QKeySequence(Qt::ALT|(Qt::Key_0+bookmark)));
        connect(recall,&QAction::triggered,this,[this,bookmark] {
            QSettings settings("KaylasSystems","NorthStarQtEditor");
            if(!applyCameraValues(viewport_,settings.value(
                sceneCameraKey(file_)+QString("/Bookmark%1").arg(bookmark))))
                statusBar()->showMessage(
                    QString("Camera bookmark %1 is empty").arg(bookmark),3500);
        });
    }
    auto* studio=viewMenu->addAction("Camera Studio...");
    studio->setObjectName("openCameraStudioAction");
    studio->setToolTip("Edit world-space cinematic camera presets for this project");
    connect(studio,&QAction::triggered,this,[this] {
        auto assets=file_.empty()?fs::path{}:file_.parent_path()/"assets";
        openCameraStudio(this,viewport_,assets);
    });
    setupOutlinerPanel(makePlaceActorMenu);
    detailsDock_=new QDockWidget("Details | Properties",this);
    detailsDock_->setObjectName("inspectorDock");
    detailsDock_->setFeatures(QDockWidget::DockWidgetClosable|
                              QDockWidget::DockWidgetMovable);
    detailsDock_->setAllowedAreas(Qt::LeftDockWidgetArea|
                                  Qt::RightDockWidgetArea);
    setupInspectorAndWorkspace(windowMenu,assetMenu,configureCharacter);
    // Reuse the existing editor QActions. No synthetic Save/Play/Gizmo
    // operations are introduced by the command palette.
    auto* commandPalette=editMenu->addAction("Find Command...");
    commandPalette->setObjectName("editorCommandPaletteAction");
    commandPalette->setShortcut(QKeySequence("Ctrl+Shift+P"));
    commandPalette->setToolTip("Search all enabled editor actions");
    connect(commandPalette,&QAction::triggered,this,[this] {
        if(embeddedPlay_)return;
        QtEditorCommandPalette palette(this);
        palette.exec();
    });

    auto* layoutMenu=windowMenu->addMenu("Editor Layouts");
    layoutMenu->setObjectName("editorLayoutsMenu");
    auto* defaultLayout=layoutMenu->addAction("Default Level Editing");
    defaultLayout->setObjectName("defaultEditorLayoutAction");
    connect(defaultLayout,&QAction::triggered,this,
            &QtEditorWindow::applyDefaultEditorLayout);

    defaultLayout->setShortcut(QKeySequence("Ctrl+Shift+1"));
    auto* assetLayout=layoutMenu->addAction("Asset Editing");
    assetLayout->setObjectName("assetEditorLayoutAction");
    assetLayout->setShortcut(QKeySequence("Ctrl+Shift+2"));
    connect(assetLayout,&QAction::triggered,this,
            &QtEditorWindow::applyAssetEditorLayout);

    auto* immersive=layoutMenu->addAction("Immersive Viewport");
    immersive->setObjectName("immersiveViewportAction");
    immersive->setShortcut(QKeySequence(Qt::Key_F11));
    immersive->setCheckable(true);
    connect(immersive,&QAction::triggered,this,
            &QtEditorWindow::toggleImmersiveViewport);

    layoutMenu->addSeparator();
    auto* saveLayout=layoutMenu->addAction("Save Layout As...");
    saveLayout->setObjectName("saveNamedEditorLayoutAction");
    connect(saveLayout,&QAction::triggered,this,[this] {
        bool ok=false;
        QString name=QInputDialog::getText(this,"Save Editor Layout",
            "Layout name:",QLineEdit::Normal,QString{},&ok).trimmed();
        if(!ok || name.isEmpty())return;
        if(name.contains('/') || name.contains('\\') || name.size()>64) {
            QMessageBox::warning(this,"Invalid Layout Name",
                "Use a name up to 64 characters without / or backslash.");
            return;
        }
        QSettings settings("KaylasSystems","NorthStarQtEditor");
        QByteArray const state=immersiveViewport_ ?
            immersiveWorkspaceBefore_ : saveState(1);
        settings.setValue("Layouts/"+name,state);
        statusBar()->showMessage("Saved editor layout: "+name,3500);
    });
    auto* savedLayouts=layoutMenu->addMenu("Load Saved Layout");
    savedLayouts->setObjectName("loadEditorLayoutMenu");
    connect(savedLayouts,&QMenu::aboutToShow,this,[this,savedLayouts] {
        savedLayouts->clear();
        QSettings settings("KaylasSystems","NorthStarQtEditor");
        settings.beginGroup("Layouts");
        QStringList names=settings.childKeys();
        settings.endGroup();
        names.sort(Qt::CaseInsensitive);
        if(names.isEmpty()) {
            auto* empty=savedLayouts->addAction("No saved layouts");
            empty->setEnabled(false);
            return;
        }
        for(QString const& name:names) {
            auto* action=savedLayouts->addAction(name);
            connect(action,&QAction::triggered,this,[this,name] {
                QSettings settings("KaylasSystems","NorthStarQtEditor");
                auto const state=settings.value("Layouts/"+name).toByteArray();
                if(state.isEmpty())return;
                if(immersiveViewport_)toggleImmersiveViewport();
                if(!restoreState(state,1)) {
                    QMessageBox::warning(this,"Editor Layout",
                        "This saved layout is no longer compatible.");
                    return;
                }
                statusBar()->showMessage("Loaded editor layout: "+name,3500);
            });
        }
    });
    layoutMenu->addSeparator();
    auto* outlinerFocus=layoutMenu->addAction("Focus World Outliner");
    outlinerFocus->setObjectName("focusWorldOutlinerAction");
    outlinerFocus->setShortcut(QKeySequence("Ctrl+Alt+O"));
    connect(outlinerFocus,&QAction::triggered,this,[this] {
        if(immersiveViewport_)toggleImmersiveViewport();
        if(outlinerDock_) {outlinerDock_->show();outlinerDock_->raise();}
        if(outlinerSearch_)outlinerSearch_->setFocus();
    });
    auto* detailsFocus=layoutMenu->addAction("Focus Details");
    detailsFocus->setObjectName("focusDetailsAction");
    detailsFocus->setShortcut(QKeySequence("Ctrl+Alt+D"));
    connect(detailsFocus,&QAction::triggered,this,[this] {
        if(immersiveViewport_)toggleImmersiveViewport();
        if(detailsDock_) {detailsDock_->show();detailsDock_->raise();}
        if(detailsDock_ && detailsDock_->widget())
            detailsDock_->widget()->setFocus();
    });
    // Ctrl+Space already toggles the native QtContentDrawer in
    // setupInspectorAndWorkspace; do not bind a second shortcut.

    auto* shortcutAction=toolsMenu->addAction("Keyboard Shortcuts...");
    shortcutAction->setObjectName("editKeyboardShortcutsAction");
    connect(shortcutAction,&QAction::triggered,this,[this] {
        if(!embeddedPlay_)shortcuts::openEditor(this);
    });
    // Apply only explicit, collision-free user overrides to the already
    // registered actions (no duplicate QAction handlers).
    shortcuts::restore(this);

}
}
