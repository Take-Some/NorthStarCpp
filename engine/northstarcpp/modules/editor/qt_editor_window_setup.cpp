#include "qt_editor_window.hpp"
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
#include "qt_text_asset_workspace.hpp"
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
namespace {
QDoubleSpinBox* scalar(QWidget* parent,double lo,double hi,int decimals=3) {
    auto* box=new QDoubleSpinBox(parent);
    box->setRange(lo,hi);
    box->setDecimals(std::max(decimals,5));
    box->setSingleStep(0.25);
    box->setKeyboardTracking(false);
    box->setMinimumWidth(72);
    return box;
}
QWidget* triplet(QWidget* parent,QDoubleSpinBox*& x,
                 QDoubleSpinBox*& y,QDoubleSpinBox*& z,double lo,double hi) {
    auto* group=new QWidget(parent);
    auto* row=new QHBoxLayout(group);
    row->setContentsMargins(0,0,0,0);
    row->setSpacing(4);
    x=scalar(group,lo,hi);y=scalar(group,lo,hi);z=scalar(group,lo,hi);
    row->addWidget(x);row->addWidget(y);row->addWidget(z);
    return group;
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
            if(version!=1 || !data || length>512)return false;
            Scene probe;
            probe.version=3;
            probe.components.push_back({0,"engine.render.light",1,
                std::string(static_cast<char const*>(data),length)});
            try {return render::scene_render_lights(probe).size()==1;}
            catch(...) {return false;}
        };
        if(!register_scene_component_type(
            {"engine.render.light",1,1,validLight,nullptr}))
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
    auto* assetMenu=menuBar()->addMenu("&Assets");
    auto* gameMenu=menuBar()->addMenu("&Game");
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
        browser_->setContentRoot({});
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
    fileMenu->addAction("E&xit",QKeySequence::Quit,this,&QWidget::close);

    undoAction_=editMenu->addAction("&Undo",QKeySequence::Undo,this,&QtEditorWindow::undo);
    redoAction_=editMenu->addAction("&Redo",QKeySequence::Redo,this,&QtEditorWindow::redo);
    editMenu->addSeparator();
    editMenu->addAction("Add &Primitive",QKeySequence(Qt::CTRL|Qt::Key_Insert),
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
        "Edit Player Blueprint...",this,[this] {
        if(selection_<0 || std::size_t(selection_)>=scene_.objects.size())
            return;
        auto const& selection=scene_.objects[std::size_t(selection_)];
        std::uint64_t pawn=selection.node.id;
        if(!player_character::has_pawn(scene_,pawn) && selection.node.parent)
            pawn=selection.node.parent;
        if(!player_character::has_pawn(scene_,pawn)) {
            if(auto* binding=player_blueprint::find(
                    scene_,selection.node.id,player_blueprint::controller_type)) {
                if(auto decoded=player_blueprint::deserialize_controller(binding->payload))
                    pawn=decoded->first;
            }
        }
        if(!player_character::has_pawn(scene_,pawn)) {
            QMessageBox::information(this,"Player Blueprint",
                "Select a Player Pawn, its Camera/SkeletalMesh, or its PlayerController.");
            return;
        }
        try {
            auto changed=player_editor::edit(this,scene_,pawn,file_.parent_path());
            if(!changed)return;
            editScene([&](Scene& scene){scene=std::move(*changed);},selection_);
            log(QString("Updated Player Blueprint Pawn #%1").arg(pawn));
        }catch(std::exception const& ex) {
            QMessageBox::warning(this,"Player Blueprint",ex.what());
        }
    });
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
    editMenu->addAction("&Duplicate Object",QKeySequence(Qt::CTRL|Qt::Key_D),
                        this,&QtEditorWindow::duplicateSelected);
    editMenu->addAction("&Rename Actor",QKeySequence(Qt::Key_F2),
                        this,&QtEditorWindow::renameSelected);
    editMenu->addAction("Delete Object",QKeySequence::Delete,
                        this,&QtEditorWindow::deleteSelected);
    assetMenu->addAction("&New Material Descriptor...",[this]{createMaterial();});
    assetMenu->addAction("New Material &Instance...",[this] {
        if(file_.empty()) {
            QMessageBox::information(this,"Project Required",
                "Save or open a scene to create a material instance.");
            return;
        }
        auto const root=native(folderRoot(file_));
        QStringList parents;
        for(auto const& ref:material_authoring::list_materials(root)) {
            if(ref.endsWith(".material.json",Qt::CaseInsensitive) ||
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
    auto* inspectSkeleton=assetMenu->addAction("Inspect Skeleton Rig...",[this] {
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
    auto* configure=gameMenu->addAction("Set Player Executable...",
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
    auto* placePanelButton=new QToolButton(toolbar);
    placePanelButton->setObjectName("placeActorsDockButton");
    placePanelButton->setText("Place Actors");
    placePanelButton->setToolTip("Show searchable Place Actors panel");
    connect(placePanelButton,&QToolButton::clicked,this,[this] {
        if(placeActors_)placeActors_->refreshCatalog();
        if(placeActorsDock_) {placeActorsDock_->show();placeActorsDock_->raise();}
    });
    toolbar->addWidget(placePanelButton);
    auto* contentButton=new QToolButton(toolbar);
    contentButton->setObjectName("toolbarContentButton");
    contentButton->setText("Content");
    contentButton->setToolTip("Open the docked Content Browser");
    connect(contentButton,&QToolButton::clicked,this,
            [this]{showContentBrowser();});
    toolbar->addWidget(contentButton);
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
    auto* shaded=viewMenu->addAction("Viewport: Shaded");
    auto* wire=viewMenu->addAction("Viewport: Wireframe");
    auto* shadedWire=viewMenu->addAction("Viewport: Shaded + Wireframe");
    auto* engineGpu=viewMenu->addAction("Viewport: Engine GPU");
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
    outlinerDock_=new QDockWidget("World Outliner",this);
    outlinerDock_->setObjectName("worldOutlinerDock");
    auto* left=new QWidget(outlinerDock_);
    auto* leftLayout=new QVBoxLayout(left);
    leftLayout->setContentsMargins(0,0,0,0);
    leftLayout->setSpacing(0);
    left->setObjectName("worldOutlinerPanel");
    auto* searchBar=new QWidget(left);
    searchBar->setObjectName("outlinerToolbar");
    auto* searchLayout=new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(9,8,9,7);
    searchLayout->setSpacing(6);
    outlinerSearch_=new QLineEdit(searchBar);
    outlinerSearch_->setObjectName("outlinerSearch");
    outlinerSearch_->setPlaceholderText("Search actors...");
    outlinerSearch_->setToolTip(
        "Search names, layers, assets, IDs. Filters: type:mesh, "
        "type:player, type:geometry, type:camera, type:light, "
        "is:hidden, is:visible.");
    outlinerSearch_->setClearButtonEnabled(true);
    outlinerSearch_->setMinimumHeight(30);
    searchLayout->addWidget(outlinerSearch_,1);
    auto* expandButton=new QToolButton(searchBar);
    expandButton->setObjectName("outlinerExpand");
    expandButton->setText("+");
    expandButton->setToolTip("Expand all actors");
    expandButton->setFixedSize(28,28);
    searchLayout->addWidget(expandButton);
    auto* collapseButton=new QToolButton(searchBar);
    collapseButton->setObjectName("outlinerCollapse");
    collapseButton->setText("-");
    collapseButton->setToolTip("Collapse actor hierarchy");
    collapseButton->setFixedSize(28,28);
    searchLayout->addWidget(collapseButton);
    auto* sortButton=new QToolButton(searchBar);
    sortButton->setObjectName("outlinerSort");
    sortButton->setText("A-Z");
    sortButton->setToolTip("Sort actors by name");
    sortButton->setCheckable(true);
    sortButton->setChecked(true);
    sortButton->setFixedSize(38,28);
    searchLayout->addWidget(sortButton);
    auto* addButton=new QToolButton(searchBar);
    addButton->setObjectName("outlinerAdd");
    addButton->setText("+");
    addButton->setToolTip("Add actor to the scene");
    addButton->setFixedSize(28,28);
    // Same category/search/action contract as the main toolbar.
    addButton->setMenu(makePlaceActorMenu(addButton));
    addButton->setPopupMode(QToolButton::InstantPopup);
    searchLayout->addWidget(addButton);
    leftLayout->addWidget(searchBar);
    outliner_=new ActorTreeWidget(left);
    outliner_->setObjectName("actorTree");
    outliner_->setHeaderHidden(true);
    outliner_->setColumnCount(2); // Actor label + narrow eye, never a Type column.
    outliner_->setAlternatingRowColors(false);
    outliner_->setMouseTracking(true);
    outliner_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    outliner_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    outliner_->setRootIsDecorated(true);
    outliner_->setIndentation(17);
    outliner_->setIconSize(QSize(16,16));
    outliner_->setAllColumnsShowFocus(false);
    outliner_->setAutoScroll(true);
    outliner_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outliner_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    outliner_->setContextMenuPolicy(Qt::CustomContextMenu);
    outliner_->setUniformRowHeights(true);
    outliner_->setDragEnabled(true);
    outliner_->setAcceptDrops(true);
    outliner_->setDropIndicatorShown(true);
    outliner_->setDragDropMode(QAbstractItemView::InternalMove);
    outliner_->setDefaultDropAction(Qt::MoveAction);
    static_cast<ActorTreeWidget*>(outliner_)->moveActor=
        [this](int actorIndex,int targetIndex,QString targetLayer) {
        if(actorIndex<0 || std::size_t(actorIndex)>=scene_.objects.size())
            return;
        if(targetIndex>=0 && std::size_t(targetIndex)>=scene_.objects.size())
            return;
        if(actorIndex==targetIndex)return;
        auto targetId=targetIndex>=0?
            scene_.objects[std::size_t(targetIndex)].node.id:std::uint64_t{0};
        // Detect cycles using stable IDs, before recording undo history.
        if(targetId) {
            auto selectedId=scene_.objects[std::size_t(actorIndex)].node.id;
            std::unordered_map<std::uint64_t,std::uint64_t> parents;
            parents.reserve(scene_.objects.size());
            for(auto const& object:scene_.objects)
                parents.emplace(object.node.id,object.node.parent);
            for(std::size_t steps=0;targetId && steps<=scene_.objects.size();++steps) {
                if(targetId==selectedId) {
                    log("Cannot attach an actor beneath its descendant.");
                    return;
                }
                auto it=parents.find(targetId);
                if(it==parents.end())break;
                targetId=it->second;
            }
        }
        auto newParent=targetIndex>=0?
            scene_.objects[std::size_t(targetIndex)].node.id:std::uint64_t{0};
        auto newLayer=targetIndex>=0?
            scene_.objects[std::size_t(targetIndex)].node.layer:
            targetLayer.isEmpty()?
                scene_.objects[std::size_t(actorIndex)].node.layer:
                utf8(targetLayer);
        if(scene_.objects[std::size_t(actorIndex)].node.parent==newParent &&
           scene_.objects[std::size_t(actorIndex)].node.layer==newLayer)
            return;
        editScene([actorIndex,newParent,newLayer](Scene& scene) {
            scene.objects[std::size_t(actorIndex)].node.parent=newParent;
            scene.objects[std::size_t(actorIndex)].node.layer=newLayer;
        },actorIndex);
    };
    static_cast<ActorTreeWidget*>(outliner_)->toggleVisibility=[this](int index) {
        if(index<0 || std::size_t(index)>=scene_.objects.size())return;
        auto const id=scene_.objects[std::size_t(index)].node.id;
        editScene([index,id](Scene& scene) {
            if(std::size_t(index)<scene.objects.size() &&
               scene.objects[std::size_t(index)].node.id==id)
                scene.objects[std::size_t(index)].node.visible=
                    !scene.objects[std::size_t(index)].node.visible;
        });
    };
    outliner_->header()->setStretchLastSection(false);
    outliner_->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    outliner_->header()->setSectionResizeMode(1,QHeaderView::Fixed);
    outliner_->header()->resizeSection(1,32);
    connect(sortButton,&QToolButton::toggled,this,[this](bool) {
        updateOutliner();
    });
    connect(expandButton,&QToolButton::clicked,outliner_,&QTreeWidget::expandAll);
    connect(collapseButton,&QToolButton::clicked,outliner_,&QTreeWidget::collapseAll);
    leftLayout->addWidget(outliner_,1);
    auto* footer=new QLabel("0 actors",left);
    footer->setObjectName("outlinerCount");
    footer->setMinimumHeight(25);
    footer->setAlignment(Qt::AlignVCenter|Qt::AlignLeft);
    leftLayout->addWidget(footer);
    outlinerDock_->setWidget(left);
    addDockWidget(Qt::LeftDockWidgetArea,outlinerDock_);
    placeActorsDock_=new QDockWidget("Place Actors",this);
    placeActorsDock_->setObjectName("placeActorsDock");
    placeActorsDock_->setFeatures(QDockWidget::DockWidgetClosable|
                                  QDockWidget::DockWidgetMovable);
    placeActorsDock_->setAllowedAreas(Qt::LeftDockWidgetArea|
                                      Qt::RightDockWidgetArea);
    placeActors_=new QtPlaceActorsPanel(placeActorsDock_);
    placeActors_->placeRequested=[this](QString const& id) {
        placeActorById(id,{0,0,0});
    };
    placeActorsDock_->setWidget(placeActors_);
    addDockWidget(Qt::LeftDockWidgetArea,placeActorsDock_);
    tabifyDockWidget(outlinerDock_,placeActorsDock_);
    outlinerDock_->raise();
    placeActorsDock_->hide();
    connect(placeActorsDock_->toggleViewAction(),&QAction::triggered,
            this,[this](bool showing) {
        if(showing && placeActors_)placeActors_->refreshCatalog();
    });

    detailsDock_=new QDockWidget("Details | Properties",this);
    detailsDock_->setObjectName("inspectorDock");
    detailsDock_->setFeatures(QDockWidget::DockWidgetClosable|
                              QDockWidget::DockWidgetMovable);
    detailsDock_->setAllowedAreas(Qt::LeftDockWidgetArea|
                                  Qt::RightDockWidgetArea);
    auto const inspector=createInspectorScroll(detailsDock_);
    auto* detailsScroll=inspector.scroll;
    auto* details=inspector.content;
    auto* form=inspector.form;
    objectName_=new QLineEdit(details);
    assetPath_=new QLineEdit(details);
    assetPath_->setPlaceholderText("Project-relative mesh reference");
    form->addRow("Actor Name",objectName_);
    form->addRow("Asset Reference",assetPath_);
    // Generic per-Entity visual prefab (independent from Player Blueprint).
    auto* visualRow=new QWidget(details);
    auto* visualLayout=new QHBoxLayout(visualRow);
    visualLayout->setContentsMargins(0,0,0,0);
    visualPrefabPath_=new QLineEdit(visualRow);
    visualPrefabPath_->setObjectName("actorVisualPrefab");
    visualPrefabPath_->setPlaceholderText("prefabs/character.nprefab");
    visualLayout->addWidget(visualPrefabPath_,1);
    auto* visualBrowse=new QPushButton("...",visualRow);
    visualBrowse->setObjectName("actorVisualPrefabBrowse");
    visualBrowse->setToolTip("Choose a visual prefab from the project or sharedAssets");
    visualLayout->addWidget(visualBrowse);
    visualPrefabApply_=new QPushButton("Apply",visualRow);
    visualPrefabApply_->setObjectName("actorVisualPrefabApply");
    visualLayout->addWidget(visualPrefabApply_);
    form->addRow("Visual Prefab",visualRow);
    connect(visualBrowse,&QPushButton::clicked,this,[this] {
        if(file_.empty()) {
            log("Save the scene before selecting a visual prefab.");return;
        }
        auto const selected=QFileDialog::getOpenFileName(this,
            "Choose Visual Prefab",qpath(file_.parent_path()),
            "NorthStar Prefabs (*.nprefab)");
        if(selected.isEmpty())return;
        auto const absolute=native(selected);
        std::vector<fs::path> roots{file_.parent_path(),
                                    file_.parent_path()/"assets"};
        auto const shared=sharedMaterialRoot();
        if(!shared.empty())roots.push_back(shared);
        for(auto const& base:roots) {
            std::error_code error;
            auto const relative=fs::relative(absolute,base,error);
            if(error || relative.empty() || relative.is_absolute())continue;
            auto const ref=relative.generic_string();
            if(ref.size()<=8 || ref.substr(ref.size()-8)!=".nprefab" ||
               !player_blueprint::safe_asset(ref))continue;
            try {
                (void)visual_prefab::locate(file_.parent_path(),ref);
                visualPrefabPath_->setText(from8(ref));return;
            }catch(std::exception const&) {}
        }
        log("Visual prefab must belong to project or sharedAssets.");
    });
    connect(visualPrefabApply_,&QPushButton::clicked,this,[this] {
        if(selection_<0 || std::size_t(selection_)>=scene_.objects.size())return;
        try {
            auto const ref=visualPrefabPath_->text().trimmed().toStdString();
            if(!ref.empty())(void)visual_prefab::load(file_.parent_path(),ref);
            auto const id=scene_.objects[std::size_t(selection_)].node.id;
            editScene([id,ref](Scene& candidate) {
                visual_prefab::bind(candidate,id,ref);
            },selection_);
        }catch(std::exception const& error) {
            log("Visual Prefab: "+QString::fromUtf8(error.what()));
        }
    });
    // Every selected scene actor has exactly one material slot, never a
    // direct texture slot. The author selects a material from project content.
    auto* materialField=new QWidget(details);
    auto* materialLayout=new QHBoxLayout(materialField);
    materialLayout->setContentsMargins(0,0,0,0);
    materialPicker_=new QComboBox(materialField);
    materialPicker_->setObjectName("actorMaterialPicker");
    materialPicker_->setIconSize(QSize(52,52));
    materialPicker_->setMinimumHeight(57);
    materialPicker_->setMaxVisibleItems(11);
    materialPicker_->setToolTip(
        "Choose a material using its PBR sphere preview; "
        "the actor receives a material reference, never a texture.");
    materialPicker_->view()->setMinimumWidth(450);
    connect(materialPicker_,QOverload<int>::of(&QComboBox::highlighted),
        this,[this](int index) {
            if(index>0)queueMaterialPreviews(index);
        });
    materialPicker_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    materialPicker_->setMinimumContentsLength(15);
    materialPicker_->setToolTip("Assign an existing material asset to this actor");
    materialLayout->addWidget(materialPicker_,1);
    materialEditButton_=new QPushButton("Edit...",materialField);
    materialEditButton_->setObjectName("editSelectedMaterial");
    materialLayout->addWidget(materialEditButton_);
    auto* refreshMaterials=new QPushButton("↻",materialField);
    refreshMaterials->setObjectName("refreshMaterials");
    refreshMaterials->setToolTip("Scan project content for available materials");
    materialLayout->addWidget(refreshMaterials);
    form->addRow("Материал",materialField);
    connect(materialPicker_,QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,[this](int index) {
            if(uiSync_ || index<0 || selection_<0 ||
               std::size_t(selection_)>=scene_.objects.size())return;
            auto const ref=materialPicker_->itemData(index).toString();
            if(ref==from8(editor_tools::actor_material(scene_,std::size_t(selection_))))
                return;
            assignMaterial(selection_,ref);
        });
    connect(materialEditButton_,&QPushButton::clicked,this,[this] {
        openMaterialEditor(materialPicker_->currentData().toString());
    });
    connect(refreshMaterials,&QPushButton::clicked,this,[this] {
        refreshMaterialChoices(true);
        updateInspector();
    });

    form->addRow("Position X/Y/Z",triplet(details,px_,py_,pz_,-900000,900000));
    form->addRow("Rotation X/Y/Z",triplet(details,rx_,ry_,rz_,-360000,360000));
    form->addRow("Scale X/Y/Z",triplet(details,sx_,sy_,sz_,0.01,1000));
    size_=scalar(details,0.05,100);
    mass_=scalar(details,0,10000);
    form->addRow("Proxy Size",size_);
    form->addRow("Mass (kg)",mass_);
    visible_=new QCheckBox("Visible",details);
    collision_=new QCheckBox("Collision enabled",details);
    form->addRow(visible_);
    form->addRow(collision_);
    auto* colliderGroup=new QGroupBox("Physics | Collision Shape",details);
    colliderGroup->setObjectName("collisionShapeGroup");
    auto* colliderForm=new QFormLayout(colliderGroup);
    colliderShape_=new QComboBox(colliderGroup);
    colliderShape_->setObjectName("collisionShape");
    colliderShape_->addItem("Box",0);
    colliderShape_->addItem("Sphere",1);
    colliderShape_->addItem("Capsule (Y axis)",2);
    colliderShape_->addItem("Complex Mesh (exact, static)",3);
    colliderShape_->addItem("Auto Convex (dynamic-safe)",4);
    colliderForm->addRow("Collider type",colliderShape_);
    colliderObjectType_=new QComboBox(colliderGroup);
    colliderObjectType_->setObjectName("collisionObjectType");
    colliderObjectType_->addItems({"WorldStatic","WorldDynamic","Pawn",
                                   "PhysicsBody","Vehicle","Destructible"});
    auto const channelLabels=collision_project::defaults().names;
    for(std::size_t i=6;i<16;++i)
        colliderObjectType_->addItem(channelLabels[i]);
    colliderForm->addRow("Object Type",colliderObjectType_);
    auto* responsesGroup=new QGroupBox("Collision Response Matrix",colliderGroup);
    responsesGroup->setObjectName("collisionResponseMatrix");
    auto* responsesLayout=new QFormLayout(responsesGroup);
    for(std::size_t i=0;i<colliderResponses_.size();++i) {
        auto* choice=new QComboBox(responsesGroup);
        choice->setObjectName(QString("collisionResponse_%1")
            .arg(channelLabels[i]));
        choice->addItems({"Ignore","Overlap","Block"});
        choice->setCurrentIndex(2);
        colliderResponses_[i]=choice;
        responsesLayout->addRow(channelLabels[i],choice);
    }
    colliderForm->addRow(responsesGroup);
    colliderPreset_=new QComboBox(colliderGroup);
    colliderPreset_->setObjectName("collisionPreset");
    colliderPreset_->addItem("Custom");
    for(auto const& preset:collision_project::defaults().presets)
        colliderPreset_->addItem(preset.name);
    colliderForm->addRow("Collision Preset",colliderPreset_);
    colliderSavePreset_=new QPushButton("Save Current As Preset...",colliderGroup);
    colliderSavePreset_->setObjectName("collisionSavePreset");
    colliderForm->addRow(colliderSavePreset_);
    colliderEditChannels_=new QPushButton("Edit Project Collision Channels...",colliderGroup);
    colliderEditChannels_->setObjectName("collisionEditChannels");
    colliderForm->addRow(colliderEditChannels_);
    colliderSplits_=new QSpinBox(colliderGroup);
    colliderSplits_->setObjectName("collisionSplitsNum");
    colliderSplits_->setRange(1,16);
    colliderSplits_->setValue(4);
    colliderSplits_->setToolTip("Maximum number of convex pieces generated from the model.");
    colliderForm->addRow("splitsNum (Auto Convex)",colliderSplits_);
    colliderHullPreview_=new QComboBox(colliderGroup);
    colliderHullPreview_->setObjectName("collisionHullPreview");
    colliderHullPreview_->addItem("All convex hulls");
    colliderForm->addRow("Preview Hull",colliderHullPreview_);
    connect(colliderHullPreview_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[this](int index) {
        if(viewport_) {
            viewport_->setCollisionHullPreview(index-1);
            if(index>0 && viewport_->drawMode()==QtEditorViewport::DrawMode::EngineGpu) {
                collisionHullPreviewForcedProxy_=true;
                viewport_->setDrawMode(QtEditorViewport::DrawMode::ShadedWireframe);
            } else if(index==0 && collisionHullPreviewForcedProxy_) {
                collisionHullPreviewForcedProxy_=false;
                viewport_->setDrawMode(QtEditorViewport::DrawMode::EngineGpu);
            }
        }
    });
    colliderForm->addRow("Size X / Y / Z (m)",
        triplet(colliderGroup,colliderX_,colliderY_,colliderZ_,.01,10000));
    colliderForm->addRow("Offset X / Y / Z (m)",
        triplet(colliderGroup,colliderOffsetX_,colliderOffsetY_,
                colliderOffsetZ_,-10000,10000));
    auto* collisionNote=new QLabel(
        "Independent of the visual mesh. Box: XYZ dimensions; "
        "sphere: diameter X; capsule: diameter X and total height Y. "
        "Use View > Collision Shapes to inspect.",colliderGroup);
    collisionNote->setWordWrap(true);
    colliderForm->addRow(collisionNote);
    colliderGenerate_=new QPushButton("Generate Collision From Mesh",colliderGroup);
    colliderGenerate_->setObjectName("collisionGenerateFromMesh");
    colliderGenerate_->setToolTip(
        "Bake OBJ/YDD/YDR triangles into this actor's collision component. "
        "For dynamic actors choose Auto Convex.");
    colliderForm->addRow(colliderGenerate_);
    connect(colliderShape_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[this](int shape) {
        colliderSplits_->setEnabled(shape==4);
        colliderGenerate_->setEnabled(shape==3 || shape==4);
    });
    colliderSplits_->setEnabled(false);
    colliderGenerate_->setEnabled(false);
    form->addRow(colliderGroup);
    auto* apply=new QPushButton("Apply Object Properties",details);
    apply->setObjectName("applyObjectProperties");
    form->addRow(apply);
    auto* characterSetup=new QPushButton(
        "Configure Character (Pawn + Motor + Camera + Capsule)",details);
    characterSetup->setObjectName("configureCharacterComponentsButton");
    characterSetup->setToolTip(
        "Add typed components to the selected actor. Legacy Player remains "
        "compatible until possession and movement use the component contracts.");
    form->addRow(characterSetup);
    connect(characterSetup,&QPushButton::clicked,configureCharacter,
            &QAction::trigger);
    auto* componentGroup=new QGroupBox("ECS Components",details);
    componentGroup->setObjectName("sceneComponentsGroup");
    auto* componentLayout=new QVBoxLayout(componentGroup);
    componentLayout->setContentsMargins(6,8,6,6);
    components_=new QTreeWidget(componentGroup);
    components_->setObjectName("sceneComponentsTree");
    components_->setHeaderLabels({"Component Type","Version","Bytes","Provider"});
    components_->setRootIsDecorated(false);
    // Fixed inner viewport: the OUTER inspector scrolls when space is
    // limited, so this nested ECS tree cannot expand its parent dock.
    components_->setMinimumHeight(0);
    components_->setFixedHeight(144);
    components_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    components_->setUniformRowHeights(true);
    components_->setSelectionMode(QAbstractItemView::SingleSelection);
    componentLayout->addWidget(components_);
    auto* componentActions=new QHBoxLayout;
    auto* addComponent=new QPushButton("Add",componentGroup);
    auto* editComponentButton=new QPushButton("Edit Raw",componentGroup);
    auto* removeComponent=new QPushButton("Remove",componentGroup);
    componentActions->addWidget(addComponent);
    componentActions->addWidget(editComponentButton);
    componentActions->addWidget(removeComponent);
    componentLayout->addLayout(componentActions);
    auto* componentNote=new QLabel(
        "Preserves unknown plugin records. Raw hexadecimal payload editing; "
        "schema-specific property widgets require plugin metadata.",componentGroup);
    componentNote->setWordWrap(true);
    componentLayout->addWidget(componentNote);
    form->addRow(componentGroup);
    connect(addComponent,&QPushButton::clicked,this,[this]{editComponent(true);});
    connect(editComponentButton,&QPushButton::clicked,this,
            [this]{editComponent(false);});
    connect(removeComponent,&QPushButton::clicked,this,
            &QtEditorWindow::deleteComponent);
    connect(components_,&QTreeWidget::itemDoubleClicked,this,
            [this](QTreeWidgetItem*,int){editComponent(false);});
    auto* hint=new QLabel("Drag mesh from Content Browser to the viewport. "
        "Hold RMB + WASD to fly, Q/E to descend/ascend, Shift to accelerate; "
        "Ctrl+1/2/3 changes gizmo mode, F focuses selection. "
        "F6 launches the saved scene in Game Player.",details);
    hint->setWordWrap(true);
    hint->setObjectName("inspectorHelpText");
    form->addRow(hint);
    detailsScroll->setWidget(details);
    detailsDock_->setWidget(inspector.panel);
    addDockWidget(Qt::RightDockWidgetArea,detailsDock_);

    contentDock_=new QDockWidget("Content Browser",this);
    contentDock_->setObjectName("contentDock");
    // A native engine editor panel: move between docking regions and tabs,
    // but never turn into a second top-level Asset Browser window.
    contentDock_->setFeatures(QDockWidget::DockWidgetClosable|
                              QDockWidget::DockWidgetMovable);
    contentDock_->setAllowedAreas(Qt::BottomDockWidgetArea|
                                  Qt::LeftDockWidgetArea|
                                  Qt::RightDockWidgetArea);
    browser_=new QtContentBrowser(contentDock_);
    wireAssetMutation(browser_);
    browser_->setEditorDockMode(true);
    // Resolve the engine's common VFS mount from the same location as
    // materials/models in Player; don't mirror sharedAssets per project.
    browser_->setSharedContentRoot(qpath(sharedMaterialRoot()));
    contentDock_->setWidget(browser_);
    addDockWidget(Qt::BottomDockWidgetArea,contentDock_);
    contentDock_->setMinimumHeight(220);
    // Reusable native text document dock: tabs are inside the Qt Editor,
    // independent of the scene viewport and immersive Play overlay.
    textWorkspace_=new QtTextAssetWorkspace(this);
    addDockWidget(Qt::BottomDockWidgetArea,textWorkspace_);
    tabifyDockWidget(contentDock_,textWorkspace_);
    textWorkspace_->hide();
    connect(textWorkspace_,&QtTextAssetWorkspace::assetSaved,this,
            [this](QString const& path,bool shaderValidated) {
        if(shaderValidated) {
            // The compiled bytes are submitted by shaderCompiled. Do not
            // reset the asset store here: that would discard cached material
            // references needed by the shader dependency graph.
            log("GLSL compiled; awaiting render provider pipeline: "+path);
        }else log("JavaScript source saved: "+path);
        if(browser_)browser_->refresh();
    });
    connect(textWorkspace_,&QtTextAssetWorkspace::shaderCompiled,this,
            [this](QString const& path,QByteArray const& spirv) {
        try {
            auto const count=viewport_->queueCompiledShader(path,spirv);
            if(!count)
                log("Shader has no active linked YMAT materials: "+path);
            else log(QString("Shader compile queued for %1 material PSO(s): %2")
                     .arg(count).arg(path));
        }catch(std::exception const& error) {
            log("Shader reload rejected: "+QString::fromUtf8(error.what()));
        }
    });
    connect(viewport_,&QtEditorViewport::shaderReloadResult,this,
            [this](QString const& result){log(result);});
    // True transient overlay, separate from the persistent dock. It cannot
    // change the dock's stored height and auto-closes on outside click/Esc.
    contentDrawer_=new QtContentDrawer(this);
    wireAssetMutation(contentDrawer_->contentBrowser());
    connect(browser_,&QtContentBrowser::contentRootsChanged,
            contentDrawer_->contentBrowser(),
            [this](QString const& project,QString const& shared) {
        if(contentDrawer_)
            contentDrawer_->setRoots(project,shared);
    });
    auto* drawerBrowser=contentDrawer_->contentBrowser();
    connect(drawerBrowser,&QtContentBrowser::assetActivated,this,
            [this](QString const& ref) {
        if(ref.endsWith(".nprefab",Qt::CaseInsensitive))
            instantiatePrefab(ref,{0,0,0});
        else if(material_authoring::is_material(ref))
            openMaterialEditor(ref);
        else log("Selected asset: "+ref);
    });
    connect(drawerBrowser,&QtContentBrowser::materialEditorRequested,this,
            [this](QString const& ref){openMaterialEditor(ref);});
    connect(drawerBrowser,&QtContentBrowser::textEditorRequested,this,
            &QtEditorWindow::openTextAsset);
    auto* showContent=assetMenu->addAction("Show Content Browser");
    showContent->setObjectName("showContentBrowserAction");
    showContent->setShortcut(QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_B));
    connect(showContent,&QAction::triggered,this,
            [this]{showContentBrowser();});
    // Content Drawer is a one-key, transient reveal of the existing
    // editor-owned dock; it never launches the standalone Asset Browser.
    auto* drawer=assetMenu->addAction("Toggle Content Drawer");
    drawer->setObjectName("toggleContentDrawerAction");
    drawer->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_Space));
    connect(drawer,&QAction::triggered,this,[this] {
        if(!contentDrawer_ || !browser_)return;
        contentDrawer_->setRoots(browser_->contentRoot(),
                                 browser_->sharedContentRoot());
        contentDrawer_->toggleDrawer();
    });

    auto* newContent=assetMenu->addAction("New Content Browser Tab");
    newContent->setObjectName("newContentBrowserTabAction");
    connect(newContent,&QAction::triggered,this,[this] {
        for(int i=2;i<=9;++i) {
            QString id=QString("contentDockInstance%1").arg(i);
            if(!findChild<QDockWidget*>(id)) {
                createContentBrowserTab(id,true);return;
            }
        }
    });
    auto* findSelected=assetMenu->addAction("Find Selected Actor Asset");
    findSelected->setObjectName("findSelectedActorAssetAction");
    connect(findSelected,&QAction::triggered,this,[this] {
        if(selection_<0 || std::size_t(selection_)>=scene_.objects.size())
            return;
        auto const& asset=scene_.objects[std::size_t(selection_)].node.asset;
        if(!asset.empty())showContentBrowser(from8(asset));
    });

    dependencyDock_=new QDockWidget("Asset References",this);
    dependencyDock_->setObjectName("dependencyDock");
    dependencies_=new QTreeWidget(dependencyDock_);
    dependencies_->setHeaderLabels({"Reference","Status"});
    dependencies_->setObjectName("assetReferenceTree");
    dependencyDock_->setWidget(dependencies_);
    addDockWidget(Qt::RightDockWidgetArea,dependencyDock_);
    tabifyDockWidget(detailsDock_,dependencyDock_);
    detailsDock_->raise();

    providerDock_=new QDockWidget("Engine Providers",this);
    providerDock_->setObjectName("engineProviderDock");
    auto* providerPanel=new QWidget(providerDock_);
    auto* providerLayout=new QVBoxLayout(providerPanel);
    auto* providerHint=new QLabel("ABI-validated resolution for NEW module instances. "
        "Running game/editor instances retain their already loaded provider.",providerPanel);
    providerHint->setWordWrap(true);
    providerLayout->addWidget(providerHint);
    auto* providerActions=new QHBoxLayout();
    auto* refreshProviders=new QPushButton("Refresh",providerPanel);
    auto* copyProviders=new QPushButton("Copy Report",providerPanel);
    auto* comparePlayer=new QPushButton("Compare with Player",providerPanel);
    comparePlayer->setObjectName("comparePlayerProviders");
    providerActions->addWidget(refreshProviders);
    providerActions->addWidget(copyProviders);
    providerActions->addWidget(comparePlayer);
    providerActions->addStretch();
    providerLayout->addLayout(providerActions);
    providers_=new QTreeWidget(providerPanel);
    providers_->setObjectName("engineProviderTree");
    providers_->setHeaderLabels({"Capability","Provider","Version","Source","ABI / Status"});
    providers_->setAlternatingRowColors(true);
    providers_->setRootIsDecorated(false);
    providers_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    providers_->header()->setStretchLastSection(true);
    providerLayout->addWidget(providers_);
    providerDock_->setWidget(providerPanel);
    addDockWidget(Qt::RightDockWidgetArea,providerDock_);
    tabifyDockWidget(dependencyDock_,providerDock_);
    providerDock_->hide();
    connect(refreshProviders,&QPushButton::clicked,this,
            &QtEditorWindow::refreshProviderInspector);
    connect(providerDock_,&QDockWidget::visibilityChanged,this,[this](bool visible){
        if(visible)refreshProviderInspector();
    });
    connect(comparePlayer,&QPushButton::clicked,this,[this] {
        const QString executable=playerExecutable();
        if(executable.isEmpty()) {
            log("Provider comparison: Player not found. Use Game > Set Player Executable.");
            return;
        }
        // The Player uses the SAME inspector/Module resolver. Launching its
        // command-line report asynchronously avoids freezing the Qt event loop.
        auto* child=new QProcess(this);
        child->setProgram(executable);
        child->setArguments({"--inspect-providers"});
        child->setWorkingDirectory(QFileInfo(executable).absolutePath());
        child->setProcessChannelMode(QProcess::MergedChannels);
        auto* timeout=new QTimer(child);
        timeout->setSingleShot(true);
        timeout->setInterval(10000);
        connect(timeout,&QTimer::timeout,this,[this,child]{
            if(child->state()!=QProcess::NotRunning) {
                log("Provider comparison timed out; terminating diagnostic Player.");
                child->kill();
            }
        });
        timeout->start();
        connect(child,&QProcess::errorOccurred,this,[this,child](QProcess::ProcessError error){
            if(error==QProcess::FailedToStart) {
                log("Provider comparison: "+child->errorString());
                child->deleteLater();
            }
        });
        connect(child,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
                this,[this,child](int exitCode,QProcess::ExitStatus exitStatus) {
            const QString output=QString::fromUtf8(child->readAllStandardOutput());
            child->deleteLater();
            if(exitStatus!=QProcess::NormalExit || exitCode!=0) {
                log("Provider comparison: Player could not produce an ABI report: "+
                    output.left(500));
                return;
            }
            QHash<QString,QString> editor;
            for(auto const& resolution:northstarcpp::inspect_engine_providers()) {
                const QString key=QString::fromStdString(resolution.capability)+"\t"+
                    QString::fromStdString(resolution.requested_provider);
                const QString signature=QString::fromStdString(resolution.provider)+"\t"+
                    QString::fromStdString(resolution.version)+"\t"+
                    QString::number(resolution.api_abi)+"\t"+
                    (resolution.available?"OK":"FAIL");
                editor.insert(key,signature);
            }
            QSet<QString> seen;
            int matches=0, mismatches=0;
            for(auto const& line:output.split('\n',Qt::SkipEmptyParts)) {
                auto const fields=line.trimmed().split('\t');
                if(fields.size()!=7 || fields[0]=="Capability")continue;
                QString const key=fields[0]+"\t"+fields[1];
                QString const signature=fields[2]+"\t"+fields[3]+"\t"+
                    fields[4]+"\t"+(fields[6]=="OK"?"OK":"FAIL");
                seen.insert(key);
                if(editor.contains(key) && editor.value(key)==signature)++matches;
                else {
                    ++mismatches;
                    log("Provider mismatch: "+fields[0]+
                        (fields[1].isEmpty()?"":" ["+fields[1]+"]")+
                        " | Player="+signature+" | Editor="+
                        editor.value(key,"unregistered"));
                }
            }
            for(auto it=editor.cbegin();it!=editor.cend();++it) {
                if(!seen.contains(it.key())) {
                    ++mismatches;
                    log("Provider missing in Player: "+it.key());
                }
            }
            log(QString("Player/Editor provider comparison: %1 matching, %2 mismatches")
                .arg(matches).arg(mismatches));
            statusBar()->showMessage(mismatches?
                QString("Provider compatibility warning: %1 mismatches").arg(mismatches):
                QString("Player and Editor provider ABI selections match (%1)").arg(matches),
                7000);
            if(providerDock_)providerDock_->show();
            refreshProviderInspector();
        });
        child->start(QIODevice::ReadOnly);
    });
    connect(copyProviders,&QPushButton::clicked,this,[this]{
        QString text="Capability\tProvider\tVersion\tSource\tABI / Status\n";
        for(int i=0;i<providers_->topLevelItemCount();++i) {
            auto* item=providers_->topLevelItem(i);
            QStringList columns;
            for(int j=0;j<5;++j)columns<<item->text(j);
            text+=columns.join('\t')+'\n';
        }
        QApplication::clipboard()->setText(text);
        statusBar()->showMessage("Engine provider report copied",3000);
    });

    auto* output=new QDockWidget("Output Log",this);
    output->setObjectName("editorOutputDock");
    messages_=new QPlainTextEdit(output);
    messages_->setReadOnly(true);
    messages_->setMaximumBlockCount(1000);
    output->setWidget(messages_);
    addDockWidget(Qt::BottomDockWidgetArea,output);
    tabifyDockWidget(contentDock_,output);
    contentDock_->raise();

    for(auto* dock:{outlinerDock_,placeActorsDock_,detailsDock_,contentDock_,
                    dependencyDock_,providerDock_,output})
        viewMenu->addAction(dock->toggleViewAction());
    viewMenu->addAction(textWorkspace_->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction("Reset Workspace Layout",[this] {
        resizeDocks({outlinerDock_,detailsDock_},{290,355},Qt::Horizontal);
        resizeDocks({contentDock_},{300},Qt::Vertical);
        for(auto* dock:{outlinerDock_,detailsDock_,contentDock_,dependencyDock_})
            dock->show();
        contentDock_->raise();detailsDock_->raise();
    });

    connect(outlinerSearch_,&QLineEdit::textChanged,this,[this](QString) {
        updateOutliner();
    });
    connect(outliner_,&QWidget::customContextMenuRequested,this,
            [this](QPoint pos) {
        auto* item=outliner_->itemAt(pos);
        auto index=item?item->data(0,Qt::UserRole).toInt():-1;
        QMenu menu(this);
        if(index>=0 && std::size_t(index)<scene_.objects.size()) {
            chooseObject(index);
            menu.addAction("Focus Actor  (F)",viewport_,
                           &QtEditorViewport::frameSelected);
            menu.addAction("Rename  (F2)",this,
                           &QtEditorWindow::renameSelected);
            menu.addAction("Duplicate  (Ctrl+D)",this,
                           &QtEditorWindow::duplicateSelected);
            menu.addAction("Delete Actor  (Delete)",this,
                           &QtEditorWindow::deleteSelected);
            auto const hidden=!scene_.objects[std::size_t(index)].node.visible;
            menu.addAction(hidden?"Show Actor":"Hide Actor",this,[this,index] {
                if(std::size_t(index)>=scene_.objects.size())return;
                editScene([index](Scene& scene) {
                    scene.objects[std::size_t(index)].node.visible=
                        !scene.objects[std::size_t(index)].node.visible;
                });
            });
            menu.addSeparator();
            auto* parentMenu=menu.addMenu("Attach To");
            parentMenu->addAction("Detach / Move To Root",[this]{
                if(selection_<0)return;
                editScene([this](Scene& scene){
                    scene.objects[std::size_t(selection_)].node.parent=0;
                });
            });
            auto const selectedId=scene_.objects[std::size_t(index)].node.id;
            std::unordered_map<std::uint64_t,std::uint64_t> parents;
            parents.reserve(scene_.objects.size());
            for(auto const& actor:scene_.objects)
                parents.emplace(actor.node.id,actor.node.parent);
            std::size_t added=0;
            for(std::size_t i=0;i<scene_.objects.size() && added<30;++i) {
                if(int(i)==index)continue;
                auto const& other=scene_.objects[i];
                // Disallow attaching under any of this actor's descendants.
                auto parent=other.node.parent;
                bool descendant=false;
                for(std::size_t guard=0;parent && guard<scene_.objects.size();++guard) {
                    if(parent==selectedId){descendant=true;break;}
                    auto it=parents.find(parent);
                    if(it==parents.end())break;
                    parent=it->second;
                }
                if(descendant)continue;
                auto targetId=other.node.id;
                parentMenu->addAction(from8(other.name),this,[this,targetId]{
                    if(selection_<0)return;
                    editScene([this,targetId](Scene& scene){
                        scene.objects[std::size_t(selection_)].node.parent=targetId;
                    });
                });
                ++added;
            }
        } else {
            menu.addAction("Add Cube Actor",[this] {
                createActor({},{0,0,0});
            });
            menu.addSeparator();
            menu.addAction("Expand All",[this]{outliner_->expandAll();});
            menu.addAction("Collapse All",[this]{outliner_->collapseAll();});
        }
        menu.exec(outliner_->viewport()->mapToGlobal(pos));
    });
    connect(outliner_,&QTreeWidget::itemSelectionChanged,this,[this] {
        if(uiSync_)return;
        selectedActorIds_.clear();
        for(auto* row:outliner_->selectedItems()) {
            auto const index=row->data(0,Qt::UserRole).toInt();
            if(index>=0 && std::size_t(index)<scene_.objects.size())
                selectedActorIds_.insert(scene_.objects[std::size_t(index)].node.id);
        }
        if(selectedActorIds_.size()>1)
            statusBar()->showMessage(
                QString("%1 actors selected · batch edit shared numeric properties")
                    .arg(selectedActorIds_.size()),5000);
        updateInspector();
    });
    connect(outliner_,&QTreeWidget::currentItemChanged,this,
            [this](QTreeWidgetItem* item,QTreeWidgetItem*) {
        if(!uiSync_)chooseObject(item?item->data(0,Qt::UserRole).toInt():-1,true);
    });
    connect(outliner_,&QTreeWidget::itemDoubleClicked,this,
            [this](QTreeWidgetItem* item,int column) {
        if(column==1 || !item)return;
        auto const index=item->data(0,Qt::UserRole).toInt();
        if(index>=0) {
            chooseObject(index);
            viewport_->frameSelected();
        }
    });
    connect(outliner_,&QTreeWidget::itemChanged,this,
            [this](QTreeWidgetItem* item,int column) {
        if(uiSync_ || column!=0 || !item)return;
        auto const index=item->data(0,Qt::UserRole).toInt();
        if(index<0 || std::size_t(index)>=scene_.objects.size())return;
        auto const id=item->data(0,Qt::UserRole+1).toULongLong();
        auto const name=item->text(0).trimmed();
        // Deferred commit avoids deleting the inline editor while Qt handles
        // its editingFinished signal. All edits go through Undo and validation.
        QTimer::singleShot(0,this,[this,index,id,name] {
            if(std::size_t(index)>=scene_.objects.size() ||
               scene_.objects[std::size_t(index)].node.id!=id)return;
            if(name.isEmpty() || name.size()>120) {
                updateOutliner();
                return;
            }
            if(from8(scene_.objects[std::size_t(index)].name)==name)return;
            editScene([index,id,name](Scene& scene) {
                if(scene.objects[std::size_t(index)].node.id==id)
                    scene.objects[std::size_t(index)].name=utf8(name);
            },index);
        });
    });
    connect(viewport_,&QtEditorViewport::selectionChanged,this,
            [this](int index) { chooseObject(index, false); });
    connect(viewport_,&QtEditorViewport::assetDropped,this,
            &QtEditorWindow::handleAssetDrop);
    connect(viewport_,&QtEditorViewport::placeableDropped,this,
            [this](QString const& id,QVector3D pos) {
        if(placeActors_ && placeActors_->hasEntry(id))
            placeActorById(id,pos);
        else log("Place Actors: rejected unknown or unavailable type "+id);
    });
    connect(viewport_,&QtEditorViewport::gizmoDragStarted,this,[this] {
        gizmoBefore_.reset();
        gizmoChanged_=false;
        gizmoRawTotal_=0.f;
        gizmoAppliedTotal_=0.f;
        gizmoInspectorTimer_.start();
    });
    connect(viewport_,&QtEditorViewport::gizmoDelta,this,
            [this](int mode,int axis,float amount) {
        if(selection_<0 || std::size_t(selection_)>=scene_.objects.size() ||
           !std::isfinite(amount))
            return;
        try {
            // Accumulate raw pointer travel before snapping. The previous
            // per-event round(amount/step) lost almost all small drags.
            gizmoRawTotal_+=amount;
            auto const options=gizmoOptions_;
            float const step=mode==0?options.translation_step:
                mode==1?options.rotation_step:options.scale_step;
            float const target=options.snap?
                std::round(gizmoRawTotal_/step)*step:gizmoRawTotal_;
            float const delta=target-gizmoAppliedTotal_;
            if(std::abs(delta)<1.e-6f)return;
            // One snapshot per gesture; render each incremental transform
            // immediately, including through the native GPU viewport.
            if(!gizmoBefore_)gizmoBefore_=scene_;
            auto incremental=options;
            incremental.snap=false;
            editor_tools::apply_gizmo(scene_,std::size_t(selection_),
                static_cast<editor_tools::GizmoMode>(mode),
                static_cast<editor_tools::GizmoAxis>(axis),delta,
                incremental,false);
            gizmoAppliedTotal_=target;
            gizmoChanged_=true;
            viewport_->setScene(&scene_);
            // The Details tree can be costly for a complex ECS scene.
            // Keep GPU/proxy visuals at pointer rate and update widgets at
            // ~30 FPS; the final drag release always synchronizes details.
            if(!gizmoInspectorTimer_.isValid() ||
                gizmoInspectorTimer_.elapsed()>=32) {
                updateInspector();
                gizmoInspectorTimer_.restart();
            }
        } catch(std::exception const& e) {
            if(gizmoBefore_)scene_=std::move(*gizmoBefore_);
            gizmoBefore_.reset();
            gizmoChanged_=false;
            gizmoRawTotal_=gizmoAppliedTotal_=0.f;
            log("Gizmo rejected: "+from8(e.what()));
            updateAll();
        }
    });
    connect(viewport_,&QtEditorViewport::gizmoDragFinished,this,[this] {
        if(gizmoBefore_ && gizmoChanged_) {
            try {
                // Full ECS/scene validation once on release, not on every
                // mouse pixel; preserve atomic rollback and a single Undo.
                validate_scene(scene_);
                history_.record(std::move(*gizmoBefore_),
                                selection_<0?0:std::size_t(selection_));
                dirty_=true;
            }catch(std::exception const& e) {
                scene_=std::move(*gizmoBefore_);
                log("Gizmo transform reverted: "+from8(e.what()));
            }
        }
        gizmoBefore_.reset();
        gizmoChanged_=false;
        gizmoRawTotal_=gizmoAppliedTotal_=0.f;
        updateAll();
    });
    connect(browser_,&QtContentBrowser::assetActivated,this,
            [this](QString ref){
        if(ref.endsWith(".nprefab",Qt::CaseInsensitive))
            instantiatePrefab(ref,{0,0,0});
        else if(isMesh(ref))createActor(ref,{0,0,0});
        else if(material_authoring::is_material(ref))openMaterialEditor(ref);
        else log("Selected asset: "+ref);
    });
    connect(browser_,&QtContentBrowser::materialEditorRequested,this,
            [this](QString const& reference){openMaterialEditor(reference);});
    connect(browser_,&QtContentBrowser::textEditorRequested,this,
            &QtEditorWindow::openTextAsset);
    connect(browser_,&QtContentBrowser::assetSelected,this,
            [this](QString ref){if(!ref.isEmpty())
                statusBar()->showMessage("Content / "+ref,2500);});
    auto commitInspector=[this] {
        if(selection_<0 || std::size_t(selection_)>=scene_.objects.size())
            return;
        auto const& current=scene_.objects[std::size_t(selection_)];
        auto name=utf8(objectName_->text().trimmed());
        auto reference=utf8(assetPath_->text().trimmed());
        if(!reference.empty() && !safe_virtual_path(reference)) {
            QMessageBox::warning(this,"Invalid Reference",
                "Asset reference must be project-relative and contain no traversal.");
            return;
        }
        auto const multi=selectedActorIds_.size()>1;
        auto changed=[&](float currentValue,QDoubleSpinBox const* input) {
            // Batch edits must be explicit; value comparison against the
            // primary actor misses changes intended for other selections.
            if(multi)
                return inspectorDirtyScalars_.count(
                    const_cast<QDoubleSpinBox*>(input))!=0;
            return std::abs(double(currentValue)-input->value())>0.000006;
        };
        auto const visibilityChanged=multi?
            inspectorDirtyChecks_.count(visible_)!=0:
            visible_->isChecked()!=current.node.visible;
        auto const collisionChanged=multi?
            inspectorDirtyChecks_.count(collision_)!=0:
            collision_->isChecked()!=current.node.collision;
        collision_authoring::Collider nextCollider;
        nextCollider.shape=static_cast<collision_authoring::Shape>(
            colliderShape_->currentData().toUInt());
        nextCollider.dimensions={float(colliderX_->value()),
            float(colliderY_->value()),float(colliderZ_->value())};
        nextCollider.offset={float(colliderOffsetX_->value()),
            float(colliderOffsetY_->value()),float(colliderOffsetZ_->value())};
        nextCollider.splits_num=std::uint32_t(colliderSplits_->value());
        for(std::size_t i=0;i<6;++i)
            nextCollider.responses[i]=static_cast<collision_authoring::Response>(
                colliderResponses_[i]->currentIndex());
        nextCollider.object_type=static_cast<collision_authoring::ObjectType>(
            colliderObjectType_->currentIndex());
        nextCollider.preset=colliderPreset_->currentIndex()>0?
            colliderPreset_->currentText().toStdString():"Custom";
        if(nextCollider.shape==collision_authoring::Shape::ComplexMesh &&
           mass_->value()>0.f && collision_->isChecked()) {
            QMessageBox::warning(this,"Complex Collision",
                "Exact triangle meshes require Mass = 0. Use Auto Convex for moving objects.");
            return;
        }
        if(!collision_authoring::valid(nextCollider)) {
            QMessageBox::warning(this,"Invalid Collision Shape",
                "Collider requires finite positive dimensions; "
                "capsule total height must be >= its diameter.");
            return;
        }
        auto const oldCollider=collision_authoring::for_object(scene_,current);
        bool const colliderAltered=
            nextCollider.shape!=oldCollider.shape ||
            nextCollider.splits_num!=oldCollider.splits_num ||
            nextCollider.object_type!=oldCollider.object_type ||
            nextCollider.responses!=oldCollider.responses ||
            nextCollider.dimensions.x!=oldCollider.dimensions.x ||
            nextCollider.dimensions.y!=oldCollider.dimensions.y ||
            nextCollider.dimensions.z!=oldCollider.dimensions.z ||
            nextCollider.offset.x!=oldCollider.offset.x ||
            nextCollider.offset.y!=oldCollider.offset.y ||
            nextCollider.offset.z!=oldCollider.offset.z;
        bool const altered=(!multi && colliderAltered) ||
            name!=current.name || reference!=current.node.asset ||
            changed(current.position.x,px_) || changed(current.position.y,py_) ||
            changed(current.position.z,pz_) ||
            changed(current.node.rotation.x,rx_) ||
            changed(current.node.rotation.y,ry_) ||
            changed(current.node.rotation.z,rz_) ||
            changed(current.node.scale.x,sx_) ||
            changed(current.node.scale.y,sy_) ||
            changed(current.node.scale.z,sz_) ||
            changed(current.size,size_) || changed(current.mass,mass_) ||
            visibilityChanged || collisionChanged;
        if(!altered)return;
        // Apply only properties explicitly changed relative to the current
        // actor; other actors may legitimately have different original values.
        auto targets=selectedActorIds_;
        if(targets.empty())targets.insert(current.node.id);
        editScene([&](Scene& scene) {
            for(auto& object:scene.objects) {
                if(!targets.count(object.node.id))continue;
                if(!multi) {
                    object.name=name;
                    object.node.asset=reference;
                }
                auto modify=[&](float& value,float primary,
                                QDoubleSpinBox const* field) {
                    if(changed(primary,field))value=float(field->value());
                };
                modify(object.position.x,current.position.x,px_);
                modify(object.position.y,current.position.y,py_);
                modify(object.position.z,current.position.z,pz_);
                modify(object.node.rotation.x,current.node.rotation.x,rx_);
                modify(object.node.rotation.y,current.node.rotation.y,ry_);
                modify(object.node.rotation.z,current.node.rotation.z,rz_);
                modify(object.node.scale.x,current.node.scale.x,sx_);
                modify(object.node.scale.y,current.node.scale.y,sy_);
                modify(object.node.scale.z,current.node.scale.z,sz_);
                modify(object.size,current.size,size_);
                modify(object.mass,current.mass,mass_);
                if(visibilityChanged)
                    object.node.visible=visible_->isChecked();
                if(collisionChanged)
                    object.node.collision=collision_->isChecked();
                if(!multi && colliderAltered)
                    collision_authoring::assign(scene,object.node.id,nextCollider);
            }
        });
    };
    connect(colliderGenerate_,&QPushButton::clicked,this,[this] {
        if(selection_<0 || std::size_t(selection_)>=scene_.objects.size() ||
           file_.empty()) {
            QMessageBox::warning(this,"Collision Generator",
                "Save the project scene and select a model actor first.");
            return;
        }
        auto const& actor=scene_.objects[std::size_t(selection_)];
        auto const shape=colliderShape_->currentData().toUInt();
        if(shape!=3u && shape!=4u)return;
        if(actor.node.asset.empty()) {
            QMessageBox::warning(this,"Collision Generator",
                "Assign an OBJ, YDD or YDR model to this actor.");
            return;
        }
        if(shape==3u && actor.mass>0.f) {
            QMessageBox::warning(this,"Collision Generator",
                "Exact triangle collision requires a static object (Mass = 0). "
                "Choose Auto Convex for moving rigid bodies.");
            return;
        }
        try {
            AssetStore assets;
            assets.mount_project_assets(file_.parent_path(),sharedMaterialRoot());
            auto baked=collision_mesh::bake(assets,actor.node.asset);
            auto const entity=actor.node.id;
            auto const splits=std::uint32_t(colliderSplits_->value());
            auto const object_type=static_cast<collision_authoring::ObjectType>(
                colliderObjectType_->currentIndex());
            auto commit=[this,entity,source=baked.source,baked,shape,splits,object_type](
                std::optional<collision_hulls::Set> hulls) {
                auto it=std::find_if(scene_.objects.begin(),scene_.objects.end(),
                    [entity](SceneObject const& o){return o.node.id==entity;});
                if(it==scene_.objects.end()||it->node.asset!=source)return;
                editScene([&](Scene& scene) {
                    auto object=std::find_if(scene.objects.begin(),scene.objects.end(),
                        [entity](SceneObject const& o){return o.node.id==entity;});
                    if(object==scene.objects.end()||object->node.asset!=source)return;
                    collision_mesh::assign(scene,entity,baked);
                    if(hulls)collision_hulls::assign(scene,entity,*hulls);
                    auto c=collision_authoring::for_object(scene,*object);
                    c.shape=static_cast<collision_authoring::Shape>(shape);
                    c.splits_num=splits;
                    c.object_type=object_type;
                    collision_authoring::assign(scene,entity,c);
                });
                log(QString("Generated %1 collision for %2")
                    .arg(shape==4u?"Auto Convex":"Complex Mesh")
                    .arg(QString::fromStdString(source)));
            };
            if(shape==4u) {
                startCollisionCook(this,std::move(baked),splits,
                    file_.parent_path()/".northstar"/"cache"/"collisions",
                    [commit](collision_hulls::Set hulls,bool) mutable {
                        commit(std::move(hulls));
                    },colliderGenerate_);
            }else commit(std::nullopt);
        } catch(std::exception const& e) {
            QMessageBox::warning(this,"Collision Generator",
                QString::fromUtf8(e.what()));
        }
    });
    connect(apply,&QPushButton::clicked,this,commitInspector);
    connect(objectName_,&QLineEdit::editingFinished,this,commitInspector);
    connect(assetPath_,&QLineEdit::editingFinished,this,commitInspector);
    for(auto* field:{px_,py_,pz_,rx_,ry_,rz_,sx_,sy_,sz_,size_,mass_}) {
        connect(field,QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,[this,field](double) {
                if(!uiSync_)inspectorDirtyScalars_.insert(field);
            });
        connect(field,&QDoubleSpinBox::editingFinished,this,
            [this,field,commitInspector] {
                inspectorDirtyScalars_.insert(field);
                commitInspector();
            });
    }
    for(auto* check:{visible_,collision_})
        connect(check,&QCheckBox::clicked,this,
            [this,check,commitInspector] {
                inspectorDirtyChecks_.insert(check);
                commitInspector();
            });
    connect(colliderPreset_,QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,[this,commitInspector](int index) {
            if(index<=0)return;
            try {
                auto settings=file_.empty()?collision_project::defaults():
                    collision_project::load(file_.parent_path());
                auto name=colliderPreset_->itemText(index);
                auto it=std::find_if(settings.presets.begin(),settings.presets.end(),
                    [&](collision_project::Preset const& p){return p.name==name;});
                if(it==settings.presets.end())return;
                {
                    QSignalBlocker blocker(colliderObjectType_);
                    colliderObjectType_->setCurrentIndex(int(it->channel));
                }
                for(std::size_t i=0;i<16;++i) {
                    QSignalBlocker blocker(colliderResponses_[i]);
                    colliderResponses_[i]->setCurrentIndex(int(it->responses[i]));
                }
                commitInspector();
            }catch(std::exception const& ex) {
                QMessageBox::warning(this,"Collision Presets",ex.what());
            }
        });
    auto markCustom=[this,commitInspector]() {
        QSignalBlocker blocker(colliderPreset_);
        colliderPreset_->setCurrentIndex(0);
        commitInspector();
    };
    connect(colliderObjectType_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[markCustom](int){markCustom();});
    for(auto* choice:colliderResponses_)
        connect(choice,QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,[markCustom](int){markCustom();});
    connect(colliderSavePreset_,&QPushButton::clicked,this,[this,commitInspector] {
        if(file_.empty()) {
            QMessageBox::warning(this,"Collision Presets",
                "Save or open a project scene before editing presets.");
            return;
        }
        bool ok=false;
        auto name=QInputDialog::getText(this,"Save Collision Preset",
            "Preset name:",QLineEdit::Normal,"NewPreset",&ok).trimmed();
        if(!ok)return;
        if(!collision_project::valid_name(name)) {
            QMessageBox::warning(this,"Collision Presets",
                "Preset names must start with a letter (A-Z, 0-9, _ or -).");
            return;
        }
        try {
            auto settings=collision_project::load(file_.parent_path());
            collision_project::Preset current;
            current.name=name;
            current.channel=std::uint32_t(colliderObjectType_->currentIndex());
            for(std::size_t i=0;i<16;++i)
                current.responses[i]=static_cast<collision_authoring::Response>(
                    colliderResponses_[i]->currentIndex());
            auto it=std::find_if(settings.presets.begin(),settings.presets.end(),
                [&](collision_project::Preset const& p){return p.name==name;});
            if(it==settings.presets.end())settings.presets.push_back(current);
            else *it=current;
            collision_project::save(file_.parent_path(),settings);
            {
                QSignalBlocker blocker(colliderPreset_);
                if(colliderPreset_->findText(name)<0)colliderPreset_->addItem(name);
                colliderPreset_->setCurrentIndex(colliderPreset_->findText(name));
            }
            commitInspector();
        }catch(std::exception const& ex) {
            QMessageBox::warning(this,"Collision Presets",ex.what());
        }
    });
    connect(colliderEditChannels_,&QPushButton::clicked,this,[this] {
        if(file_.empty()) {
            QMessageBox::warning(this,"Collision Channels",
                "Open a project scene before editing channel names.");
            return;
        }
        try {
            if(collision_project::edit_names(this,file_.parent_path()))
                updateInspector();
        }catch(std::exception const& ex) {
            QMessageBox::warning(this,"Collision Channels",ex.what());
        }
    });
    connect(colliderSplits_,QOverload<int>::of(&QSpinBox::valueChanged),
            this,[commitInspector](int){commitInspector();});
    connect(colliderObjectType_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[commitInspector](int){commitInspector();});
    connect(colliderShape_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[commitInspector](int){commitInspector();});
    for(auto* field:{colliderX_,colliderY_,colliderZ_,colliderOffsetX_,
                     colliderOffsetY_,colliderOffsetZ_})
        connect(field,&QDoubleSpinBox::editingFinished,this,commitInspector);
    statusBar()->showMessage(
        "Editor ready · RMB+WASD fly · Q/E vertical · View modes · F6 Play Game");
}
}
