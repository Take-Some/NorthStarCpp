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
void QtEditorWindow::setupInspectorAndWorkspace(
    QMenu* viewMenu,QMenu* assetMenu,QAction* configureCharacter) {
    auto const inspector=createInspectorScroll(detailsDock_);
    auto* detailsScroll=inspector.scroll;
    auto* details=inspector.content;
    auto* form=inspector.form;
    // The Details dock already has a persistent search bar outside the
    // scroll content. Reuse it instead of introducing duplicate search UI.
    detailsSearch_=inspector.search;
    detailsSearch_->setPlaceholderText("Search Details: transform, material, collision...");
    detailsSearch_->setToolTip(
        "Search names and groups. Use group:Physics, field:Mass, "
        "quotes for phrases, - to exclude, and @favorites for pinned groups.");
    // The original generic row filter is superseded by category-aware
    // filtering below; two QFormLayout controllers would fight each other.
    QObject::disconnect(detailsSearch_,nullptr,form,nullptr);
    auto* filterFeedback=new QLabel(details);
    filterFeedback->setObjectName("detailsSearchFeedback");
    filterFeedback->setWordWrap(true);
    filterFeedback->setVisible(false);
    form->addRow(filterFeedback);
    struct DetailSection {QString key;int begin;QToolButton* header;};
    auto sections=std::make_shared<std::vector<DetailSection>>();
    auto addSection=[&](QString key,QString title) {
        auto* header=new QToolButton(details);
        header->setObjectName("detailsCategory"+key);
        QSettings favoriteSettings("KaylasSystems","NorthStarQtEditor");
        const bool favorite=favoriteSettings.value("Details/Favorites/"+key,false).toBool();
        header->setText(favorite?QString::fromUtf8("★ ")+title:title);
        header->setProperty("northstarDetailsTitle",title);
        header->setToolTip("Right-click to pin this property group. Search @favorites to filter.");
        header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header->setAutoRaise(true);
        header->setCheckable(true);
        QSettings settings("KaylasSystems","NorthStarQtEditor");
        header->setChecked(settings.value("Details/Expanded/"+key,true).toBool());
        header->setArrowType(header->isChecked()?Qt::DownArrow:Qt::RightArrow);
        form->addRow(header);
        sections->push_back({key,form->rowCount(),header});
        return header;
    };
    addSection("Actor","ACTOR / IDENTITY");

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
    addSection("Appearance","APPEARANCE / MATERIAL");
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

    addSection("Transform","TRANSFORM");
    form->addRow("Position X/Y/Z",triplet(details,px_,py_,pz_,-900000,900000));
    form->addRow("Rotation X/Y/Z",triplet(details,rx_,ry_,rz_,-360000,360000));
    form->addRow("Scale X/Y/Z",triplet(details,sx_,sy_,sz_,0.01,1000));
    size_=scalar(details,0.05,100);
    mass_=scalar(details,0,10000);
    form->addRow("Proxy Size",size_);
    form->addRow("Mass (kg)",mass_);
    visible_=new QCheckBox("Visible",details);
    collision_=new QCheckBox("Collision enabled",details);
    addSection("Visibility","VISIBILITY");
    form->addRow(visible_);
    form->addRow(collision_);
    lightSettings_=new QGroupBox("Light | Dynamic Scene Source",details);
    lightSettings_->setObjectName("lightSettingsGroup");
    auto* lightingForm=new QFormLayout(lightSettings_);
    lightType_=new QComboBox(lightSettings_);
    lightType_->setObjectName("lightType");
    lightType_->addItems({"Point Light","Spot Light","Directional Light","Sky Light"});
    lightingForm->addRow("Type",lightType_);
    lightIntensity_=scalar(lightSettings_,0,100000,3);
    lightIntensity_->setObjectName("lightIntensity");
    lightingForm->addRow("Intensity",lightIntensity_);
    lightRange_=scalar(lightSettings_,.1,100000,2);
    lightRange_->setObjectName("lightRange");
    lightingForm->addRow("Radius / Range",lightRange_);
    auto* color=triplet(lightSettings_,lightR_,lightG_,lightB_,0,100);
    lightR_->setObjectName("lightRed");
    lightG_->setObjectName("lightGreen");
    lightB_->setObjectName("lightBlue");
    lightingForm->addRow("Color (linear RGB)",color);
    auto* direction=triplet(lightSettings_,lightDirX_,lightDirY_,lightDirZ_,-1,1);
    lightDirX_->setObjectName("lightDirectionX");
    lightDirY_->setObjectName("lightDirectionY");
    lightDirZ_->setObjectName("lightDirectionZ");
    lightingForm->addRow("Direction XYZ",direction);
    lightCone_=scalar(lightSettings_,1,89,1);
    lightCone_->setObjectName("lightConeDegrees");
    lightCone_->setSuffix(" deg");
    lightingForm->addRow("Outer Cone Angle",lightCone_);
    lightApply_=new QPushButton("Apply Light",lightSettings_);
    lightApply_->setObjectName("applyLightSettings");
    lightingForm->addRow(lightApply_);
    lightSettings_->setVisible(false);
    addSection("Lighting","LIGHTING");
    form->addRow(lightSettings_);
    connect(lightType_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[this](int kind) {
        if(lightRange_)lightRange_->setEnabled(kind==0||kind==1);
        if(lightCone_)lightCone_->setEnabled(kind==1);
        for(auto* box:{lightDirX_,lightDirY_,lightDirZ_})
            if(box)box->setEnabled(kind==1||kind==2);
    });
    connect(lightApply_,&QPushButton::clicked,this,[this] {
        if(selection_<0 || std::size_t(selection_)>=scene_.objects.size() ||
           selectedActorIds_.size()>1)return;
        auto const id=scene_.objects[std::size_t(selection_)].node.id;
        const auto selected=std::find_if(scene_.components.begin(),
            scene_.components.end(),[&](SceneComponentBlob const& item){
                return item.type=="engine.render.light" && item.entity_id==id;
            });
        if(selected==scene_.components.end())return;
        try {
            auto light=render::decode_scene_light(*selected);
            light.kind=static_cast<render::LightKind>(lightType_->currentIndex());
            auto& gpu=light.gpu;
            gpu.color_intensity[0]=float(lightR_->value());
            gpu.color_intensity[1]=float(lightG_->value());
            gpu.color_intensity[2]=float(lightB_->value());
            gpu.color_intensity[3]=float(lightIntensity_->value());
            if(light.kind==render::LightKind::Point ||
               light.kind==render::LightKind::Spot)
                gpu.position_range[3]=float(lightRange_->value());
            else gpu.position_range[3]=1.f;
            auto direction=QVector3D(float(lightDirX_->value()),
                float(lightDirY_->value()),float(lightDirZ_->value()));
            if(light.kind==render::LightKind::Spot ||
               light.kind==render::LightKind::Directional){
                if(direction.lengthSquared()<1.e-8f)
                    throw std::invalid_argument("Light direction must be nonzero");
                direction.normalize();
            }
            gpu.direction_cone[0]=direction.x();
            gpu.direction_cone[1]=direction.y();
            gpu.direction_cone[2]=direction.z();
            constexpr double rad=3.14159265358979323846/180.0;
            gpu.direction_cone[3]=light.kind==render::LightKind::Point?-1.f:
                light.kind==render::LightKind::Directional?-2.f:
                light.kind==render::LightKind::Sky?-3.f:
                float(std::cos(lightCone_->value()*rad));
            auto payload=render::encode_scene_light(light);
            editScene([id,payload](Scene& candidate){
                for(auto& component:candidate.components)
                    if(component.entity_id==id && component.type=="engine.render.light") {
                        component.version=2;
                        component.payload=payload;
                        return;
                    }
                throw std::runtime_error("Light actor component was removed");
            },selection_);
        }catch(std::exception const& error) {
            QMessageBox::warning(this,"Light Properties",error.what());
        }
    });
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
    addSection("Physics","PHYSICS / COLLISION");
    form->addRow(colliderGroup);
    auto* apply=new QPushButton("Apply Object Properties",details);
    apply->setObjectName("applyObjectProperties");
    auto* characterSetup=new QPushButton(
        "Configure Character (Pawn + Motor + Camera + Capsule)",details);
    characterSetup->setObjectName("configureCharacterComponentsButton");
    characterSetup->setToolTip(
        "Add typed components to the selected actor. Legacy Player remains "
        "compatible until possession and movement use the component contracts.");
    addSection("Components","COMPONENTS / ECS");
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
    form->addRow(apply);
    form->addRow(hint);
    // Search and collapse only QFormLayout rows, leaving editor-owned
    // field instances and their existing Undo/Redo/dirty tracking intact.
    // Expanding a section never commits an inspector edit.
    updateDetailsFilter_=[this,form,sections,filterFeedback,apply,hint] {
        if(!detailsSearch_)return;
        QString const query=detailsSearch_->text().trimmed();
        bool const favoritesOnly=query.compare("@favorites",Qt::CaseInsensitive)==0;
        QSettings favoriteSettings("KaylasSystems","NorthStarQtEditor");
        static QRegularExpression const token(
            R"ns((-?)(?:(group|field):)?(?:"([^"]+)"|([^\s]+)))ns",
            QRegularExpression::CaseInsensitiveOption);
        auto const terms=token.globalMatch(query);
        int matches=0;
        for(std::size_t i=0;i<sections->size();++i) {
            auto const& section=(*sections)[i];
            int end=i+1<sections->size()
                ?(*sections)[i+1].begin-1:form->rowCount();
            bool matchesAny=false;
            bool const lighting=section.key=="Lighting";
            bool lightSelected=false;
            if(lighting && selection_>=0 &&
               std::size_t(selection_)<scene_.objects.size() &&
               selectedActorIds_.size()<=1) {
                auto const id=scene_.objects[std::size_t(selection_)].node.id;
                for(auto const& item:scene_.components)
                    if(item.entity_id==id && item.type=="engine.render.light") {
                        lightSelected=true;break;
                    }
            }
            for(int row=section.begin;row<end;++row) {
                auto const* labelItem=form->itemAt(row,QFormLayout::LabelRole);
                auto const* widgetItem=form->itemAt(row,QFormLayout::FieldRole);
                QWidget* field=widgetItem?widgetItem->widget():nullptr;
                if(!field)continue;
                if(field==apply || field==hint) {
                    form->setRowVisible(row,query.isEmpty());
                    continue;
                }
                QString fieldSearch=field->objectName();
                QString const groupSearch=section.key+" "+section.header->text();
                auto appendText=[&fieldSearch](QWidget* widget) {
                    if(auto* label=qobject_cast<QLabel*>(widget))
                        fieldSearch+=" "+label->text();
                    else if(auto* group=qobject_cast<QGroupBox*>(widget))
                        fieldSearch+=" "+group->title();
                    else if(auto* button=qobject_cast<QAbstractButton*>(widget))
                        fieldSearch+=" "+button->text();
                };
                appendText(field);
                if(labelItem && labelItem->widget())appendText(labelItem->widget());
                for(auto* child:field->findChildren<QWidget*>()) {
                    fieldSearch+=" "+child->objectName();
                    appendText(child);
                }
                QString const searchable=groupSearch+" "+fieldSearch;
                bool match=!favoritesOnly ||
                    favoriteSettings.value("Details/Favorites/"+section.key,false).toBool();
                for(auto term=terms;!favoritesOnly && term.hasNext();) {
                    auto const part=term.next();
                    auto const scope=part.captured(2).toLower();
                    auto const needle=part.captured(3).isEmpty()?part.captured(4):part.captured(3);
                    auto const& haystack=scope=="group"?groupSearch:
                        scope=="field"?fieldSearch:searchable;
                    bool const contains=haystack.contains(needle,Qt::CaseInsensitive);
                    if(part.captured(1).isEmpty()!=contains){match=false;break;}
                }
                matchesAny|=match;
                if(match && (!lighting || lightSelected))++matches;
                bool const show=match && (query.isEmpty()?
                    section.header->isChecked():true) &&
                    (!lighting || lightSelected);
                form->setRowVisible(row,show);
            }
            section.header->setVisible((!lighting || lightSelected) &&
                (query.isEmpty() || matchesAny));
            section.header->setArrowType(section.header->isChecked()?
                Qt::DownArrow:Qt::RightArrow);
        }
        filterFeedback->setText(matches==0?
            (favoritesOnly?"No favorite groups pinned. Right-click a group heading to pin it.":
            "No properties match this search. Try group:Transform or field:Mass."):
            QString("%1 matching properties").arg(matches));
        // The search feedback is outside section-owned rows and must not
        // be collapsed alongside ordinary property controls.
        filterFeedback->setVisible(!query.isEmpty());
    };
    connect(detailsSearch_,&QLineEdit::textChanged,this,
            [this]{if(updateDetailsFilter_)updateDetailsFilter_();});
    auto* groupMenu=new QMenu(detailsSearch_);
    auto setGroupsExpanded=[this,sections](bool expanded) {
        for(auto const& section:*sections)section.header->setChecked(expanded);
        if(updateDetailsFilter_)updateDetailsFilter_();
    };
    groupMenu->addAction("Expand Property Groups",this,[setGroupsExpanded] {
        setGroupsExpanded(true);
    });
    groupMenu->addAction("Collapse Property Groups",this,[setGroupsExpanded] {
        setGroupsExpanded(false);
    });
    groupMenu->addAction("Show Favorite Property Groups",this,[this] {
        detailsSearch_->setText("@favorites");
    });
    connect(detailsSearch_,&QWidget::customContextMenuRequested,this,
            [this,groupMenu](QPoint const& point) {
        std::unique_ptr<QMenu> menu(detailsSearch_->createStandardContextMenu());
        menu->addSeparator();
        menu->addActions(groupMenu->actions());
        menu->exec(detailsSearch_->mapToGlobal(point));
    });
    detailsSearch_->setContextMenuPolicy(Qt::CustomContextMenu);
    for(auto const& section:*sections) {
        connect(section.header,&QToolButton::toggled,this,
                [this,key=section.key](bool expanded) {
            QSettings settings("KaylasSystems","NorthStarQtEditor");
            settings.setValue("Details/Expanded/"+key,expanded);
            if(updateDetailsFilter_)updateDetailsFilter_();
        });
        section.header->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(section.header,&QWidget::customContextMenuRequested,this,
                [this,header=section.header,key=section.key](QPoint const& point) {
            QSettings settings("KaylasSystems","NorthStarQtEditor");
            auto const setting="Details/Favorites/"+key;
            QMenu menu(header);
            auto* favorite=menu.addAction("Pin Group to Favorites");
            favorite->setCheckable(true);
            favorite->setChecked(settings.value(setting,false).toBool());
            if(menu.exec(header->mapToGlobal(point))!=favorite)return;
            const bool pinned=favorite->isChecked();
            settings.setValue(setting,pinned);
            const QString title=header->property("northstarDetailsTitle").toString();
            header->setText(pinned?QString::fromUtf8("★ ")+title:title);
            if(updateDetailsFilter_)updateDetailsFilter_();
        });
    }
    updateDetailsFilter_();
    detailsScroll->setWidget(details);
    detailsDock_->setWidget(inspector.panel);
    addDockWidget(Qt::RightDockWidgetArea,detailsDock_);
    auto* focusDetails=viewMenu->addAction("Focus Details Search");
    focusDetails->setObjectName("focusDetailsSearchAction");
    focusDetails->setShortcut(QKeySequence(Qt::CTRL|Qt::ALT|Qt::Key_F));
    connect(focusDetails,&QAction::triggered,this,[this] {
        detailsDock_->show();
        detailsDock_->raise();
        detailsSearch_->setFocus(Qt::ShortcutFocusReason);
        detailsSearch_->selectAll();
    });

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
    // All source and shader editing happens in a dedicated modal window.
    // Content Browser remains a navigation and selection dock.
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
    connect(drawerBrowser,&QtContentBrowser::uiAssetEditorRequested,this,
            &QtEditorWindow::openUiAsset);
    auto* showContent=viewMenu->addAction("Content Browser");
    showContent->setObjectName("showContentBrowserAction");
    showContent->setShortcut(QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_B));
    connect(showContent,&QAction::triggered,this,
            [this]{showContentBrowser();});
    // Content Drawer is a one-key, transient reveal of the existing
    // editor-owned dock; it never launches the standalone Asset Browser.
    auto* drawer=viewMenu->addAction("Content Drawer");
    drawer->setObjectName("toggleContentDrawerAction");
    drawer->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_Space));
    connect(drawer,&QAction::triggered,this,[this] {
        if(!contentDrawer_ || !browser_)return;
        contentDrawer_->setRoots(browser_->contentRoot(),
                                 browser_->sharedContentRoot());
        contentDrawer_->toggleDrawer();
    });

    auto* newContent=viewMenu->addAction("New Content Browser Tab");
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
    // Asset authoring uses a modal document editor instead of a dock tab.
    viewMenu->addSeparator();
    viewMenu->addAction("Reset Workspace Layout",[this] {
        resizeDocks({outlinerDock_,detailsDock_},{290,355},Qt::Horizontal);
        resizeDocks({contentDock_},{300},Qt::Vertical);
        for(auto* dock:{outlinerDock_,detailsDock_,contentDock_,dependencyDock_})
            dock->show();
        contentDock_->raise();detailsDock_->raise();
    });

    wireWorkspaceSignals(apply);
}
} // namespace northstarcpp::editor
