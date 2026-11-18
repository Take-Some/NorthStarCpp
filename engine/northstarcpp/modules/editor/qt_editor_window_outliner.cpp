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
void QtEditorWindow::setupOutlinerPanel(
    std::function<QMenu*(QWidget*)> const& makePlaceActorMenu) {
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
        "type:pawn, type:controller, type:motor, type:camera, type:light, "
        "is:startup, is:hidden, is:visible.");
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


}
} // namespace northstarcpp::editor
