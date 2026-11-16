#include "qt_editor_window.hpp"
#include "editor_visual_prefab.hpp"
#include "qt_place_actors_panel.hpp"
#include <northstarcpp/scene_camera_actor.hpp>
#include <northstarcpp/scene_pawn.hpp>
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
#include <northstarcpp/player_character_components.hpp>
#include <northstarcpp/player_blueprint.hpp>
#include <northstarcpp/collision_authoring.hpp>
#include <northstarcpp/collision_mesh.hpp>
#include <northstarcpp/collision_hulls.hpp>
#include "qt_collision_project_settings.hpp"
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
#include "qt_text_asset_editor.hpp"
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
#include <iterator>
#include <iostream>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <string>
#include <sstream>

namespace northstarcpp::editor {
namespace fs=std::filesystem;
using namespace window_detail;
namespace {
// Vector-painted, editor-owned glyphs: no bright Windows file icons.
QIcon paintOutlinerGlyph(int kind) {
    QPixmap canvas(18,18);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#8ea9c7"),1.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    if(kind==0) { // Actor layer
        p.setBrush(QColor("#52677d"));
        p.drawRoundedRect(QRectF(1.6,5.2,14.8,10),1.8,1.8);
        p.drawRoundedRect(QRectF(1.7,3.7,7.6,4.2),1.1,1.1);
    } else if(kind==1) { // Player
        p.setBrush(QColor("#7197bf"));
        p.drawEllipse(QRectF(6.2,2.2,5.4,5.4));
        p.drawRoundedRect(QRectF(3.5,10.1,11.0,5.7),2.4,2.4);
    } else if(kind==2 || kind==4) { // Procedural / primitive
        p.setBrush(QColor("#36546e"));
        QPolygonF top({{9,2.5},{15,6},{9,9.1},{3,6}});
        p.drawPolygon(top);
        p.drawLine(QPointF(3,6),QPointF(3,12.5));
        p.drawLine(QPointF(3,12.5),QPointF(9,16));
        p.drawLine(QPointF(9,16),QPointF(15,12.5));
        p.drawLine(QPointF(15,12.5),QPointF(15,6));
        p.drawLine(QPointF(9,9.1),QPointF(9,16));
    } else if(kind==5) { // Independent scene camera
        p.setPen(QPen(QColor("#9dcee8"),1.35));
        p.setBrush(QColor("#365a73"));
        p.drawRoundedRect(QRectF(1.8,5,11.1,9),1.9,1.9);
        p.drawPolygon(QPolygonF{{13.3,7},{17,5.4},{17,13.5},{13.3,11.7}});
        p.drawEllipse(QRectF(6.1,7,3.4,4.1));
    } else if(kind==6) { // Point light
        p.setPen(QPen(QColor("#f0d584"),1.55));
        p.setBrush(QColor("#b78b37"));
        p.drawEllipse(QPointF(9,9),3.4,3.4);
        for(int i=0;i<8;++i) {
            constexpr float pi=3.14159265358979323846f;
            auto const theta=float(i)*pi/4.f;
            p.drawLine(QPointF(9+5.5*std::cos(theta),9+5.5*std::sin(theta)),
                       QPointF(9+7.5*std::cos(theta),9+7.5*std::sin(theta)));
        }
    } else if(kind==7) { // Spot light
        p.setPen(QPen(QColor("#f1d996"),1.5));
        p.setBrush(QColor("#947338"));
        p.drawRoundedRect(QRectF(2,2.8,8,6.4),1.7,1.7);
        p.drawPolygon(QPolygonF{{9.7,4.2},{16.5,1.9},{16.5,15.2},{9.7,8.5}});
    } else { // Mesh / imported asset
        p.setBrush(QColor("#355c75"));
        p.drawRoundedRect(QRectF(3.1,2.7,11.8,12.7),1.5,1.5);
        p.setPen(QPen(QColor("#a0c9dc"),1.2));
        p.drawLine(QPointF(5,12),QPointF(8,7.4));
        p.drawLine(QPointF(8,7.4),QPointF(10,10.6));
        p.drawLine(QPointF(10,10.6),QPointF(13,6.5));
    }
    return QIcon(canvas);
}
QIcon paintOutlinerEyeGlyph(bool visible) {
    QPixmap canvas(18,18);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    auto const color=visible?QColor("#9aafc4"):QColor("#627184");
    p.setPen(QPen(color,1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    QPainterPath eye;
    eye.moveTo(1.4,9);
    eye.cubicTo(5.2,3.4,12.8,3.4,16.6,9);
    eye.cubicTo(12.8,14.6,5.2,14.6,1.4,9);
    p.drawPath(eye);
    p.setBrush(color);
    p.drawEllipse(QPointF(9,9),2,2);
    if(!visible) {
        p.setPen(QPen(QColor("#d0948b"),2.0,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(QPointF(3,15),QPointF(15,3));
    }
    return QIcon(canvas);
}
// Rasterize each stable Outliner icon once rather than for every actor in every
// scene refresh. QIcon is implicitly shared and cheap to copy.
QIcon outlinerGlyph(int kind) {
    static const auto cached=[] {
        std::array<QIcon,8> icons;
        for(std::size_t i=0;i<icons.size();++i)
            icons[i]=paintOutlinerGlyph(static_cast<int>(i));
        return icons;
    }();
    return cached[static_cast<std::size_t>(kind>=0 && kind<=7?kind:3)];
}
QIcon outlinerEyeGlyph(bool visible) {
    static const std::array<QIcon,2> cached{
        paintOutlinerEyeGlyph(false),paintOutlinerEyeGlyph(true)};
    return cached[visible?1u:0u];
}
// Validate material component references before accepting scene edits.
bool validateMaterialComponent(void*,std::uint32_t version,
                               void const* payload,std::size_t size) {
    if(version!=1 || !payload || size==0 || size>1024)return false;
    std::string ref(static_cast<char const*>(payload),size);
    return safe_virtual_path(ref) &&
        material_authoring::is_material(from8(ref));
}
}
QtEditorWindow::QtEditorWindow(QWidget* parent):QMainWindow(parent) {
    setObjectName("northstarQtEditor");
    setWindowTitle("NorthStar Editor — Qt");
    resize(1600,940);
    setMinimumSize(960,610);
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks |
                   QMainWindow::AnimatedDocks |
                   QMainWindow::GroupedDragging);
    scene_=make_startup_scene();
    // Smooth, per-pixel gizmo interaction by default. Snapping remains
    // explicitly available in the authoring toolbar.
    gizmoOptions_.snap=false;
    editor_tools::promote_to_v3(scene_);
    register_geometry_primitive_component();
    (void)player_character::register_schemas();
    (void)player_blueprint::register_schemas();
    (void)register_scene_component_type({
        editor_tools::actor_material_component_type,1,1,
        validateMaterialComponent,nullptr});


    (void)register_scene_component_type({
        collision_authoring::component_type,1,4,
        [](void*,std::uint32_t v,void const* d,std::size_t n)->bool {
            return (v>=1 && v<=4) && d && n>0 && n<=512 &&
                collision_authoring::deserialize(std::string(static_cast<char const*>(d),n),v).has_value();
        },nullptr});
    setupUi();
    restoreWorkspace();
    updateAll();
}
QString QtEditorWindow::sceneTitle() const {
    return QString("NorthStar Editor — %1%2")
        .arg(file_.empty()?"Untitled":qpath(file_.filename()),dirty_?" *":"");
}
void QtEditorWindow::log(QString const& line) {
    if(messages_)messages_->appendPlainText(line);
    statusBar()->showMessage(line,6000);
    // QA-only diagnostics survive a Windows GUI-subsystem process without
    // an attached stdout/stderr console; avoid disk I/O for normal UI logs.
    if(line.startsWith("smoke:")) {
        auto const name=QString("NorthStarQtEditor-smoke-%1.log")
            .arg(QCoreApplication::applicationPid());
        QFile report(QDir(QDir::tempPath()).filePath(name));
        if(report.open(QIODevice::WriteOnly|QIODevice::Append)) {
            report.write(line.toUtf8());
            report.write("\n",1);
        }
    }
}
void QtEditorWindow::updateOutliner() {
    QSignalBlocker block(outliner_);
    uiSync_=true;
    // Capture open/closed branches only from the unfiltered tree. While
    // searching, expand ancestors but do NOT destroy saved expansion state.
    auto const wasFiltered=outliner_->property("outlinerFiltered").toBool();
    QSet<QString> collapsed;
    for(auto const& key:outliner_->property("outlinerCollapsedKeys").toStringList())
        collapsed.insert(key);
    if(!wasFiltered) {
        collapsed.clear();
        QTreeWidgetItemIterator previous(outliner_);
        while(*previous) {
            auto* item=*previous;
            if(item->childCount()>0 && !item->isExpanded()) {
                auto const id=item->data(0,Qt::UserRole).toInt();
                collapsed.insert(id>=0?
                    QString("actor:%1").arg(item->data(0,Qt::UserRole+1).toULongLong()):
                    QString("layer:%1").arg(item->text(0)));
            }
            ++previous;
        }
    }
    QStringList saved;
    for(auto const& key:collapsed)saved.append(key);
    outliner_->setProperty("outlinerCollapsedKeys",saved);
    outliner_->setProperty("outlinerFiltered",!outlinerSearch_->text().trimmed().isEmpty());
    outliner_->setSortingEnabled(false);
    outliner_->clear();
    QString const filter=outlinerSearch_->text().trimmed();
    std::unordered_map<std::uint64_t,std::size_t> byId;
    byId.reserve(scene_.objects.size());
    for(std::size_t i=0;i<scene_.objects.size();++i)
        byId.emplace(scene_.objects[i].node.id,i);
    for(auto it=selectedActorIds_.begin();it!=selectedActorIds_.end();)
        it=byId.count(*it)?std::next(it):selectedActorIds_.erase(it);
    if(selectedActorIds_.empty() && selection_>=0 &&
       std::size_t(selection_)<scene_.objects.size())
        selectedActorIds_.insert(scene_.objects[std::size_t(selection_)].node.id);
    std::unordered_set<std::uint64_t> geometryIds,cameraIds,pointLightIds,spotLightIds;
    geometryIds.reserve(scene_.components.size());
    for(auto const& component:scene_.components) {
        if(component.type==geometry_primitive_component)
            geometryIds.insert(component.entity_id);
        else if(component.type==scene_camera::type)
            cameraIds.insert(component.entity_id);
        else if(component.type=="engine.render.light") {
            // Cone cosine is the 12th scalar in the current light bridge.
            std::istringstream input(component.payload);
            float value=0.f,cone=-1.f;
            bool valid=true;
            for(int i=0;i<12;++i)
                if(!(input>>value)) {valid=false;break;}
                else if(i==11)cone=value;
            if(valid && cone>=0.f)spotLightIds.insert(component.entity_id);
            else pointLightIds.insert(component.entity_id);
        }
    }

    // Search by name, layer, asset, entity ID and useful UE-style qualifiers:
    // "type:mesh", "type:player", "type:geometry", "is:hidden".
    // simplified() normalizes whitespace without constructing a regex per refresh.
    auto terms=filter.simplified().split(' ',Qt::SkipEmptyParts);
    auto matches=[&](SceneObject const& actor) {
        auto const procedural=geometryIds.count(actor.node.id)!=0;
        QString const kind=player_character::has_pawn(scene_,actor.node.id)?"player":
            cameraIds.count(actor.node.id)?"camera":
            (pointLightIds.count(actor.node.id)||spotLightIds.count(actor.node.id))?"light":
            procedural?"geometry":actor.node.asset.empty()?"primitive":"mesh";
        QString nameText,layerText,assetText,idText;
        bool textDecoded=false;
        for(auto const& term:terms) {
            if(term.startsWith("type:",Qt::CaseInsensitive)) {
                auto const type=term.mid(5);
                if(!kind.contains(type,Qt::CaseInsensitive))return false;
            } else if(term.compare("is:hidden",Qt::CaseInsensitive)==0) {
                if(actor.node.visible)return false;
            } else if(term.compare("is:visible",Qt::CaseInsensitive)==0) {
                if(!actor.node.visible)return false;
            } else {
                // Decode UTF-8 fields once per actor, not once per search term.
                if(!textDecoded) {
                    nameText=from8(actor.name);
                    layerText=from8(actor.node.layer);
                    assetText=from8(actor.node.asset);
                    idText=QString::number(actor.node.id);
                    textDecoded=true;
                }
                if(!nameText.contains(term,Qt::CaseInsensitive) &&
                   !layerText.contains(term,Qt::CaseInsensitive) &&
                   !assetText.contains(term,Qt::CaseInsensitive) &&
                   !idText.contains(term) &&
                   !kind.contains(term,Qt::CaseInsensitive))return false;
            }
        }
        return true;
    };
    std::vector<bool> visible(scene_.objects.size(),filter.isEmpty());
    if(!filter.isEmpty()) {
        for(std::size_t i=0;i<scene_.objects.size();++i) {
            auto const& object=scene_.objects[i];
            if(!matches(object))continue;
            visible[i]=true;
            // A match in a child must also show its ancestor chain.
            auto parent=object.node.parent;
            std::size_t guard=0;
            while(parent && guard++<scene_.objects.size()) {
                auto it=byId.find(parent);
                if(it==byId.end() || it->second==i)break;
                if(visible[it->second])break;
                visible[it->second]=true;
                parent=scene_.objects[it->second].node.parent;
            }
        }
    }
    std::unordered_map<std::string,QTreeWidgetItem*> layers;
    std::vector<QTreeWidgetItem*> entries(scene_.objects.size(),nullptr);
    QTreeWidgetItem* active=nullptr;
    for(std::size_t i=0;i<scene_.objects.size();++i) {
        if(!visible[i])continue;
        auto const& object=scene_.objects[i];
        bool const procedural=geometryIds.count(object.node.id)!=0;
        auto* item=new QTreeWidgetItem({from8(object.name)});
        item->setData(0,Qt::UserRole,int(i));
        item->setData(0,Qt::UserRole+1,qulonglong(object.node.id));
        item->setFlags(item->flags()|Qt::ItemIsEditable);
        auto const kind=player_character::has_pawn(scene_,object.node.id)?"Player":
            cameraIds.count(object.node.id)?"Camera":
            pointLightIds.count(object.node.id)?"Point Light":
            spotLightIds.count(object.node.id)?"Spot Light":
            procedural?"Geometry":object.node.asset.empty()?"Primitive":"Static Mesh";
        item->setToolTip(0,QString("%1  |  ID %2\nLayer: %3")
                              .arg(kind).arg(object.node.id)
                              .arg(from8(object.node.layer)));
        item->setToolTip(1,object.node.visible?"Hide in Editor":"Show in Editor");
        item->setIcon(0,outlinerGlyph(
            player_character::has_pawn(scene_,object.node.id)?1:
            cameraIds.count(object.node.id)?5:
            pointLightIds.count(object.node.id)?6:
            spotLightIds.count(object.node.id)?7:
            procedural?2:object.node.asset.empty()?4:3));
        item->setIcon(1,outlinerEyeGlyph(object.node.visible));
        item->setTextAlignment(1,Qt::AlignCenter);
        if(!object.node.visible)
            item->setForeground(0,QBrush(QColor("#8393a6")));
        entries[i]=item;
        if(int(i)==selection_)active=item;
    }
    for(std::size_t i=0;i<scene_.objects.size();++i) {
        if(!entries[i])continue;
        auto const& object=scene_.objects[i];
        QTreeWidgetItem* parent=nullptr;
        if(object.node.parent) {
            auto it=byId.find(object.node.parent);
            if(it!=byId.end() && it->second!=i)
                parent=entries[it->second];
        }
        if(parent) {
            parent->addChild(entries[i]);
        } else {
            auto layer=object.node.layer.empty()?
                std::string("Default"):object.node.layer;
            auto it=layers.find(layer);
            if(it==layers.end()) {
                auto* folder=new QTreeWidgetItem(outliner_,{from8(layer)});
                folder->setData(0,Qt::UserRole,-1);
                folder->setIcon(0,outlinerGlyph(0));
                folder->setToolTip(0,"Actor layer: "+from8(layer));
                folder->setForeground(0,QBrush(QColor("#b6c6d9")));
                QFont layerFont=folder->font(0);
                layerFont.setWeight(QFont::DemiBold);
                folder->setFont(0,layerFont);
                folder->setExpanded(true);
                it=layers.emplace(layer,folder).first;
            }
            it->second->addChild(entries[i]);
        }
    }
    if(auto* sorter=outlinerDock_->findChild<QToolButton*>("outlinerSort");
       sorter && sorter->isChecked())
        outliner_->sortItems(0,Qt::AscendingOrder);
    if(!filter.isEmpty())
        outliner_->expandAll();
    else {
        QTreeWidgetItemIterator iter(outliner_);
        while(*iter) {
            auto* item=*iter;
            if(item->childCount()>0) {
                auto const index=item->data(0,Qt::UserRole).toInt();
                auto const key=index>=0?
                    QString("actor:%1").arg(item->data(0,Qt::UserRole+1).toULongLong()):
                    QString("layer:%1").arg(item->text(0));
                item->setExpanded(!collapsed.contains(key));
            }
            ++iter;
        }
    }
    if(auto* count=outlinerDock_->findChild<QLabel*>("outlinerCount")) {
        auto const shown=std::count(visible.begin(),visible.end(),true);
        count->setText(filter.isEmpty()
            ?QString("%1 actors  |  %2 layers")
                .arg(scene_.objects.size()).arg(layers.size())
            :QString("%1 of %2 actors")
                .arg(shown).arg(scene_.objects.size()));
    }
    if(active)
        outliner_->setCurrentItem(active);
    for(std::size_t i=0;i<entries.size();++i)
        if(entries[i] && selectedActorIds_.count(scene_.objects[i].node.id))
            entries[i]->setSelected(true);
    uiSync_=false;
}
void QtEditorWindow::refreshProviderInspector() {
    if(!providers_)return;
    providers_->clear();
    int available=0;
    for(auto const& status: northstarcpp::inspect_engine_providers()) {
        QString capability=QString::fromStdString(status.capability);
        if(!status.requested_provider.empty())
            capability+=" ["+QString::fromStdString(status.requested_provider)+"]";
        auto* row=new QTreeWidgetItem(providers_,{
            capability,
            QString::fromStdString(status.provider),
            QString::fromStdString(status.version),
            QString::fromStdString(status.source),
            QString::number(status.api_abi)+
                (status.available?" / OK":" / Unavailable")
        });
        if(status.available)++available;
        if(!status.error.empty()) {
            auto const message=QString::fromStdString(status.error);
            for(int c=0;c<5;++c)row->setToolTip(c,message);
            row->setForeground(4,QBrush(QColor("#e8a56e")));
        }
        if(status.external)row->setForeground(3,QBrush(QColor("#8dd3ae")));
    }
    statusBar()->showMessage(QString("Engine provider resolution: %1/%2 compatible")
        .arg(available).arg(providers_->topLevelItemCount()),4000);
}

void QtEditorWindow::updateInspector() {
    inspectorDirtyScalars_.clear();
    inspectorDirtyChecks_.clear();
    updateComponentInspector();
    bool valid=selection_>=0 && std::size_t(selection_)<scene_.objects.size();
    for(QWidget* w:std::initializer_list<QWidget*>{objectName_,assetPath_,
                     visualPrefabPath_,visualPrefabApply_,
                     px_,py_,pz_,rx_,ry_,rz_,sx_,sy_,sz_,size_,mass_,
                     visible_,collision_,materialPicker_,colliderShape_,
                     colliderX_,colliderY_,colliderZ_,colliderOffsetX_,
                     colliderOffsetY_,colliderOffsetZ_,colliderObjectType_,
                     colliderSplits_,colliderGenerate_,colliderHullPreview_,
                     colliderPreset_,colliderSavePreset_,colliderEditChannels_})
        w->setEnabled(valid);
    refreshMaterialChoices();
    QSignalBlocker materialBlock(materialPicker_);
    if(!valid) {
        QSignalBlocker a(objectName_),b(assetPath_),c(visualPrefabPath_);
        visualPrefabPath_->clear();
        materialPicker_->setCurrentIndex(0);
        materialEditButton_->setEnabled(false);
        objectName_->clear();assetPath_->clear();return;
    }
    auto const& o=scene_.objects[std::size_t(selection_)];
    auto const material=from8(editor_tools::actor_material(
        scene_,static_cast<std::size_t>(selection_)));
    int materialIndex=materialPicker_->findData(material);
    if(materialIndex<0 && !material.isEmpty()) {
        materialPicker_->addItem("Missing: "+material,material);
        materialIndex=materialPicker_->count()-1;
    }
    materialPicker_->setCurrentIndex(std::max(materialIndex,0));
    // The selected actor may use a material far below the initial popup
    // screen. Prioritize that sphere even when the project has many assets.
    if(materialPicker_->currentIndex()>0)
        queueMaterialPreviews(materialPicker_->currentIndex());
    materialEditButton_->setEnabled(
        material.endsWith(".material.json",Qt::CaseInsensitive) &&
        cachedMaterials_.contains(material));
    QSignalBlocker a(objectName_),b(assetPath_),c(visualPrefabPath_);
    visualPrefabPath_->setText(from8(visual_prefab::reference(scene_,o.node.id)));
    objectName_->setText(from8(o.name));
    assetPath_->setText(from8(o.node.asset));
    auto const actorScalars=[](SceneObject const& actor) {
        return std::array<float,11>{actor.position.x,actor.position.y,
            actor.position.z,actor.node.rotation.x,actor.node.rotation.y,
            actor.node.rotation.z,actor.node.scale.x,actor.node.scale.y,
            actor.node.scale.z,actor.size,actor.mass};
    };
    std::array<QDoubleSpinBox*,11> const scalarFields{
        px_,py_,pz_,rx_,ry_,rz_,sx_,sy_,sz_,size_,mass_};
    auto const primary=actorScalars(o);
    auto const multi=selectedActorIds_.size()>1;
    std::array<bool,11> mixed{};
    bool mixedVisible=false,mixedCollision=false;
    if(multi) {
        for(auto const& actor:scene_.objects) {
            if(!selectedActorIds_.count(actor.node.id))continue;
            auto const values=actorScalars(actor);
            for(std::size_t i=0;i<scalarFields.size();++i)
                if(std::abs(values[i]-primary[i])>0.00001f)mixed[i]=true;
            mixedVisible |= actor.node.visible!=o.node.visible;
            mixedCollision |= actor.node.collision!=o.node.collision;
        }
    }
    for(std::size_t i=0;i<scalarFields.size();++i) {
        auto* field=scalarFields[i];
        QSignalBlocker blocker(field);
        field->setSuffix(mixed[i]?QStringLiteral("  *"):QString{});
        field->setProperty("mixedValues",mixed[i]);
        field->setValue(primary[i]);
    }
    {
        QSignalBlocker visibleBlock(visible_),collisionBlock(collision_);
        visible_->setTristate(multi);
        collision_->setTristate(multi);
        visible_->setCheckState(mixedVisible?Qt::PartiallyChecked:
            o.node.visible?Qt::Checked:Qt::Unchecked);
        collision_->setCheckState(mixedCollision?Qt::PartiallyChecked:
            o.node.collision?Qt::Checked:Qt::Unchecked);
    }
    auto const col=collision_authoring::for_object(scene_,o);
    QSignalBlocker blockCollider(colliderShape_);
    colliderShape_->setCurrentIndex(int(col.shape));
    QSignalBlocker typeBlock(colliderObjectType_);
    QSignalBlocker splitsBlock(colliderSplits_);
    colliderObjectType_->setCurrentIndex(int(col.object_type));
    colliderSplits_->setValue(int(col.splits_num));
    {
        auto settings=collision_project::defaults();
        try {
            if(!file_.empty())settings=collision_project::load(file_.parent_path());
        }catch(std::exception const&) {}
        QSignalBlocker blocked(colliderPreset_);
        colliderPreset_->clear();
        colliderPreset_->addItem("Custom");
        for(auto const& preset:settings.presets)
            colliderPreset_->addItem(preset.name);
        auto index=colliderPreset_->findText(QString::fromStdString(col.preset));
        colliderPreset_->setCurrentIndex(index<0?0:index);
        for(std::size_t i=0;i<16;++i) {
            colliderObjectType_->setItemText(int(i),settings.names[i]);
            if(auto* form=qobject_cast<QFormLayout*>(
                   colliderResponses_[i]->parentWidget()->layout()))
                if(auto* label=qobject_cast<QLabel*>(
                       form->labelForField(colliderResponses_[i])))
                    label->setText(settings.names[i]);
        }
    }
    {
        QSignalBlocker previewBlock(colliderHullPreview_);
        colliderHullPreview_->clear();
        colliderHullPreview_->addItem("All convex hulls");
        if(auto cooked=collision_hulls::lookup(scene_,o.node.id);
           cooked && cooked->source==o.node.asset &&
           cooked->splits_num==col.splits_num)
            for(std::size_t h=0;h<cooked->hulls.size();++h)
                colliderHullPreview_->addItem(QString("Hull %1 (%2 vertices)")
                    .arg(h+1).arg(cooked->hulls[h].xyz.size()/3));
    }
    if(viewport_) {
        viewport_->setCollisionHullPreview(-1);
        if(collisionHullPreviewForcedProxy_) {
            viewport_->setDrawMode(QtEditorViewport::DrawMode::EngineGpu);
            collisionHullPreviewForcedProxy_=false;
        }
    }
    for(std::size_t i=0;i<colliderResponses_.size();++i) {
        QSignalBlocker blocked(colliderResponses_[i]);
        colliderResponses_[i]->setCurrentIndex(int(col.responses[i]));
    }
    colliderSplits_->setEnabled(col.shape==collision_authoring::Shape::AutoConvex);
    colliderGenerate_->setEnabled(col.shape==collision_authoring::Shape::AutoConvex ||
                                  col.shape==collision_authoring::Shape::ComplexMesh);
    colliderX_->setValue(col.dimensions.x);
    colliderY_->setValue(col.dimensions.y);
    colliderZ_->setValue(col.dimensions.z);
    colliderOffsetX_->setValue(col.offset.x);
    colliderOffsetY_->setValue(col.offset.y);
    colliderOffsetZ_->setValue(col.offset.z);
    // Names and asset references are per-actor, not shared batch properties.
    objectName_->setEnabled(!multi);
    assetPath_->setEnabled(!multi);
    visualPrefabPath_->setEnabled(!multi);
    visualPrefabApply_->setEnabled(!multi);
    materialPicker_->setEnabled(!multi);
    if(multi) {
        materialEditButton_->setEnabled(false);
        colliderGenerate_->setEnabled(false);
    }
    auto const hint=multi?
        QString("Multiple actors: editing a value applies it to every selection. "
                "Unchanged fields retain each actor's original value.") :
        QString();
    for(auto* field:scalarFields)
        field->setToolTip(field->property("mixedValues").toBool()?
            QString("Mixed values (*): editing sets one value for all selected "
                    "actors; other properties are preserved.") : hint);
    for(auto* field:{visible_,collision_})
        field->setToolTip(field->checkState()==Qt::PartiallyChecked?
            QString("Mixed boolean values: click to assign to all actors."):hint);
    // Collider authoring currently replaces one full compound record.
    // Prevent overwriting non-primary actors until per-field patching exists.
    for(auto* field:std::initializer_list<QWidget*>{colliderShape_,
        colliderObjectType_,colliderSplits_,colliderX_,colliderY_,colliderZ_,
        colliderOffsetX_,colliderOffsetY_,colliderOffsetZ_,colliderHullPreview_})
        field->setEnabled(!multi);
}
void QtEditorWindow::updateComponentInspector() {
    if(!components_)return;
    QSignalBlocker block(components_);
    int const previous=components_->currentItem()?
        components_->currentItem()->data(0,Qt::UserRole).toInt():-1;
    components_->clear();
    std::uint64_t entity=0;
    if(selection_>=0 && std::size_t(selection_)<scene_.objects.size())
        entity=scene_.objects[std::size_t(selection_)].node.id;
    QTreeWidgetItem* restore=nullptr;
    for(std::size_t i=0;i<scene_.components.size();++i) {
        auto const& entry=scene_.components[i];
        if(entry.entity_id!=entity)continue;
        auto schema=scene_component_schema(entry.type);
        QString provider="Opaque / Unavailable";
        if(schema) {
            provider=entry.version>=schema->min_version &&
                     entry.version<=schema->max_version ?
                     "Registered" : "Unknown Version";
        }
        auto* row=new QTreeWidgetItem(components_,{
            from8(entry.type),QString::number(entry.version),
            QString::number(entry.payload.size()),provider});
        row->setData(0,Qt::UserRole,static_cast<int>(i));
        row->setToolTip(0,QString("Entity %1; %2 (binary retained verbatim)")
                            .arg(entry.entity_id).arg(provider));
        if(provider!="Registered")
            row->setForeground(3,QColor("#e4bd7d"));
        if(static_cast<int>(i)==previous)restore=row;
    }
    if(restore)components_->setCurrentItem(restore);
    components_->resizeColumnToContents(1);
    components_->resizeColumnToContents(2);
}
void QtEditorWindow::editComponent(bool create) {
    if(!components_)return;
    int index=-1;
    if(!create && components_->currentItem()) {
        index=components_->currentItem()->data(0,Qt::UserRole).toInt();
        if(index<0 || std::size_t(index)>=scene_.components.size())return;
    }
    if(!create && index<0)return;
    SceneComponentBlob existing;
    if(index>=0)existing=scene_.components[std::size_t(index)];
    else {
        if(selection_>=0 && std::size_t(selection_)<scene_.objects.size())
            existing.entity_id=scene_.objects[std::size_t(selection_)].node.id;
        auto schemas=list_scene_component_schemas();
        if(!schemas.empty()) {
            existing.type=schemas.front().type;
            existing.version=schemas.front().min_version;
        } else {
            existing.type="com.example.component";
        }
    }
    if(existing.payload.size()>131072) {
        QMessageBox::information(this,"Large Binary Component",
            "This payload exceeds the interactive raw-editor limit (128 KiB). "
            "Its original bytes remain intact. Use a plugin-specific editor "
            "or a streaming binary tool to modify it.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(create?"Add ECS Component":"Edit ECS Component");
    dialog.resize(650,480);
    auto* fields=new QFormLayout(&dialog);
    auto* type=new QComboBox(&dialog);
    type->setObjectName("componentSchemaType");
    type->setEditable(true);
    type->setInsertPolicy(QComboBox::NoInsert);
    for(auto const& schema:list_scene_component_schemas())
        type->addItem(from8(schema.type));
    type->setCurrentText(from8(existing.type));
    fields->addRow("Stable Type ID",type);
    auto* version=new QLineEdit(QString::number(existing.version),&dialog);
    version->setObjectName("componentSchemaVersion");
    fields->addRow("Schema Version",version);
    auto* payload=text_assets::createSourceEditor(&dialog,text_assets::Format::Plain,false);
    payload->setObjectName("componentHexPayload");
    payload->setPlainText(QString::fromLatin1(
        QByteArray(existing.payload.data(),int(existing.payload.size())).toHex(' ')));
    payload->setPlaceholderText("Hexadecimal bytes (empty is permitted)");
    payload->setMinimumHeight(230);
    fields->addRow("Binary Payload (hex)",payload);
    // Reflection metadata is a value snapshot: no plugin function pointers
    // survive provider unload. Raw bytes remain the lossless source of truth.
    auto* typedGroup=new QGroupBox("Typed ECS Properties",&dialog);
    typedGroup->setObjectName("componentTypedProperties");
    auto* typedForm=new QFormLayout(typedGroup);
    std::vector<std::pair<SceneComponentField,QDoubleSpinBox*>> typedControls;
    auto const currentSchema=scene_component_schema(existing.type);
    if(currentSchema && existing.version>=currentSchema->min_version &&
       existing.version<=currentSchema->max_version) {
        for(auto const& field:currentSchema->editor_fields) {
            auto const scalar=read_scene_component_field(existing,field);
            if(!scalar)continue; // missing/unknown payload remains opaque
            auto* control=new QDoubleSpinBox(typedGroup);
            control->setObjectName(
                "componentProperty_"+from8(field.label).replace(' ','_'));
            auto const integer=field.kind==SceneComponentFieldKind::UInt32;
            control->setDecimals(integer?0:6);
            control->setRange(integer?0.0:-1000000.0,
                              integer?4294967295.0:1000000.0);
            control->setSingleStep(integer?1.0:0.1);
            control->setKeyboardTracking(false);
            control->setValue(integer?
                static_cast<double>(std::get<std::uint32_t>(*scalar)):
                static_cast<double>(std::get<float>(*scalar)));
            typedForm->addRow(from8(field.label),control);
            typedControls.emplace_back(field,control);
            connect(control,QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                &dialog,[payload,existing,field,integer](double input) {
                    auto hex=payload->toPlainText();
                    hex.remove(QRegularExpression("\\s+"));
                    if(hex.size()%2 ||
                       hex.contains(QRegularExpression("[^0-9a-fA-F]")))return;
                    auto const bytes=QByteArray::fromHex(hex.toLatin1());
                    SceneComponentBlob value=existing;
                    value.payload.assign(bytes.constData(),
                                         static_cast<std::size_t>(bytes.size()));
                    SceneComponentFieldValue scalar=integer?
                        SceneComponentFieldValue{
                            static_cast<std::uint32_t>(input)}:
                        SceneComponentFieldValue{static_cast<float>(input)};
                    if(!write_scene_component_field(value,field,scalar))return;
                    QSignalBlocker guard(payload);
                    payload->setPlainText(QString::fromLatin1(
                        QByteArray(value.payload.data(),
                                   static_cast<int>(value.payload.size()))
                            .toHex(' ')));
                });
        }
    }
    typedGroup->setVisible(!typedControls.empty());
    if(!typedControls.empty()) {
        fields->addRow(typedGroup);
        // Keep typed controls consistent when raw bytes are pasted by hand.
        connect(payload,&QPlainTextEdit::textChanged,&dialog,
            [payload,typedGroup,typedControls] {
                auto hex=payload->toPlainText();
                hex.remove(QRegularExpression("\\s+"));
                bool const valid=hex.size()%2==0 &&
                    !hex.contains(QRegularExpression("[^0-9a-fA-F]"));
                auto const bytes=valid?QByteArray::fromHex(hex.toLatin1()):
                    QByteArray{};
                SceneComponentBlob current;
                current.payload.assign(bytes.constData(),
                                       static_cast<std::size_t>(bytes.size()));
                for(auto const& pair:typedControls) {
                    auto const scalar=valid?
                        read_scene_component_field(current,pair.first):
                        std::nullopt;
                    pair.second->setEnabled(scalar.has_value());
                    if(scalar) {
                        QSignalBlocker blocker(pair.second);
                        pair.second->setValue(
                            std::holds_alternative<std::uint32_t>(*scalar)?
                            static_cast<double>(std::get<std::uint32_t>(*scalar)):
                            static_cast<double>(std::get<float>(*scalar)));
                    }
                }
                typedGroup->setToolTip(valid?QString{}:
                    QString("Fix invalid hex bytes before editing properties"));
            });
        auto toggleTyped=[type,version,typedGroup,existing] {
            typedGroup->setVisible(
                type->currentText()==QString::fromStdString(existing.type) &&
                version->text()==QString::number(existing.version));
        };
        connect(type,&QComboBox::currentTextChanged,&dialog,toggleTyped);
        connect(version,&QLineEdit::textChanged,&dialog,toggleTyped);
    }
    auto* schemaMessage=new QLabel(&dialog);
    schemaMessage->setWordWrap(true);
    fields->addRow(schemaMessage);
    auto refreshSchema=[type,schemaMessage] {
        auto info=scene_component_schema(utf8(type->currentText().trimmed()));
        schemaMessage->setText(info?
            QString("Plugin schema registered: versions %1-%2. "
                    "Validation will run before committing.")
                .arg(info->min_version).arg(info->max_version):
            QString("Provider unavailable: payload is opaque. "
                    "Existing component bytes are never interpreted."));
    };
    connect(type,&QComboBox::currentTextChanged,&dialog,refreshSchema);
    refreshSchema();
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|
                                      QDialogButtonBox::Cancel,&dialog);
    fields->addRow(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    SceneComponentBlob edited;
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        bool ok=false;
        auto ver=version->text().trimmed().toUInt(&ok);
        if(!ok || ver==0) {
            QMessageBox::warning(&dialog,"Invalid Version",
                "Component version must be between 1 and 4294967295.");
            return;
        }
        auto hex=payload->toPlainText();
        hex.remove(QRegularExpression("\\s+"));
        if(hex.size()%2 || hex.contains(QRegularExpression("[^0-9a-fA-F]")) ||
           hex.size()>2097152) {
            QMessageBox::warning(&dialog,"Invalid Binary Payload",
                "Enter even-length hexadecimal bytes, up to 1 MiB.");
            return;
        }
        auto bytes=QByteArray::fromHex(hex.toLatin1());
        edited.entity_id=existing.entity_id;
        edited.type=utf8(type->currentText().trimmed());
        edited.version=ver;
        edited.payload.assign(bytes.constData(),std::size_t(bytes.size()));
        if(!validate_scene_component_blob(edited)) {
            QMessageBox::warning(&dialog,"Component Validation Failed",
                "The plugin schema rejected this component or its payload.");
            return;
        }
        dialog.accept();
    });
    if(dialog.exec()!=QDialog::Accepted)return;
    if(create) {
        editScene([&](Scene& scene) {
            // Entity ID may change on a v1/v2 scene promotion.
            editor_tools::promote_to_v3(scene);
            if(selection_>=0 && std::size_t(selection_)<scene.objects.size())
                edited.entity_id=scene.objects[std::size_t(selection_)].node.id;
            scene.components.push_back(edited);
        });
    } else {
        editScene([&](Scene& scene) {
            scene.components[std::size_t(index)]=edited;
        });
    }
}
void QtEditorWindow::deleteComponent() {
    if(!components_ || !components_->currentItem())return;
    auto index=components_->currentItem()->data(0,Qt::UserRole).toInt();
    if(index<0 || std::size_t(index)>=scene_.components.size())return;
    auto const name=from8(scene_.components[std::size_t(index)].type);
    if(QMessageBox::question(this,"Remove ECS Component",
        "Remove "+name+" from this entity? The operation is undoable.",
        QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)
        return;
    editScene([index](Scene& scene) {
        scene.components.erase(scene.components.begin()+index);
    });
}
void QtEditorWindow::updateDependencies() {
    dependencies_->clear();
    if(file_.empty())return;
    QDir root(folderRoot(file_));
    auto const shared=sharedMaterialRoot();
    for(std::size_t i=0;i<scene_.objects.size();++i) {
        auto const& object=scene_.objects[i];
        if(!object.node.asset.empty()) {
            auto const ref=from8(object.node.asset);
            auto const physical=ref.section('@',0,0);
            auto const found=QFileInfo::exists(root.filePath(physical)) ||
                QFileInfo::exists(QDir(qpath(shared)).filePath(physical));
            auto* row=new QTreeWidgetItem(dependencies_,{
                ref,found?"Available":"Missing"});
            row->setForeground(1,QBrush(QColor(found?"#8bc98b":"#ed8a8a")));
            row->setToolTip(0,"Mesh: "+from8(object.name));
        }
        auto const material=from8(editor_tools::actor_material(scene_,i));
        if(material.isEmpty())continue;
        auto const found=availableMaterial(native(folderRoot(file_)),material) ||
                         availableMaterial(shared,material);
        auto* row=new QTreeWidgetItem(dependencies_,{
            material,found?"Available Material":"Missing Material"});
        row->setForeground(1,QBrush(QColor(found?"#8bc98b":"#ed8a8a")));
        row->setToolTip(0,"Assigned material for: "+from8(object.name));
    }
    dependencies_->resizeColumnToContents(0);
}
void QtEditorWindow::updateAll() {
    viewport_->setScene(&scene_);
    viewport_->setSelected(selection_);
    updateOutliner();
    updateInspector();
    updateDependencies();
    undoAction_->setEnabled(history_.can_undo());
    redoAction_->setEnabled(history_.can_redo());
    setWindowTitle(sceneTitle());
    statusBar()->showMessage(QString("%1 actors  ·  Scene V%2")
       .arg(scene_.objects.size()).arg(scene_.version),4000);
}
void QtEditorWindow::chooseObject(int index,bool preserveMulti) {
    if(index<0 || std::size_t(index)>=scene_.objects.size())index=-1;
    selection_=index;
    if(!preserveMulti) {
        selectedActorIds_.clear();
        if(index>=0)selectedActorIds_.insert(scene_.objects[std::size_t(index)].node.id);
    }
    viewport_->setSelected(index);
    // Never destroy the tree as a result of a selection signal: this used to
    // invalidate rows during mouse events and reset scrolling unnecessarily.
    auto locate=[this,index]() -> QTreeWidgetItem* {
        QTreeWidgetItemIterator it(outliner_);
        while(*it) {
            if((*it)->data(0,Qt::UserRole).toInt()==index && index>=0)
                return *it;
            ++it;
        }
        return nullptr;
    };
    auto* selected=locate();
    if(!selected && index>=0 && !outlinerSearch_->text().isEmpty()) {
        outlinerSearch_->clear(); // Expose a viewport-selected filtered-out actor.
        selected=locate();
    }
    {
        QSignalBlocker block(outliner_);
        if(!preserveMulti)outliner_->clearSelection();
        if(selected) {
            for(auto* parent=selected->parent();parent;parent=parent->parent())
                parent->setExpanded(true);
            outliner_->setCurrentItem(selected);
            if(!preserveMulti)selected->setSelected(true);
            outliner_->scrollToItem(selected,QAbstractItemView::EnsureVisible);
        } else
            outliner_->setCurrentItem(nullptr);
    }
    updateInspector();
}
void QtEditorWindow::renameSelected() {
    if(selection_<0 || std::size_t(selection_)>=scene_.objects.size())return;
    QTreeWidgetItemIterator it(outliner_);
    while(*it) {
        auto* item=*it;
        if(item->data(0,Qt::UserRole).toInt()==selection_) {
            for(auto* parent=item->parent();parent;parent=parent->parent())
                parent->setExpanded(true);
            outliner_->setCurrentItem(item);
            outliner_->scrollToItem(item,QAbstractItemView::EnsureVisible);
            outliner_->editItem(item,0); // F2 = immediate in-place edit as in UE.
            return;
        }
        ++it;
    }
    if(!outlinerSearch_->text().isEmpty()) {
        outlinerSearch_->clear();
        renameSelected();
    }
}
void QtEditorWindow::duplicateSelected() {
    if(selection_<0 || std::size_t(selection_)>=scene_.objects.size())return;
    int next=-1;
    editScene([this,&next](Scene& scene) {
        next=int(editor_tools::duplicate(scene,std::size_t(selection_)));
    });
    if(next>=0)chooseObject(next);
}
void QtEditorWindow::deleteSelected() {
    if(selection_<0 || std::size_t(selection_)>=scene_.objects.size())return;
    editScene([this](Scene& scene) {
        editor_tools::delete_subtree(scene,std::size_t(selection_));
    },-1);
}
void QtEditorWindow::editScene(std::function<void(Scene&)> change,int selectedAfter) {
    try {
        Scene next=scene_;
        change(next);
        validate_scene(next);
        history_.record(scene_,selection_<0?0:std::size_t(selection_));
        scene_=std::move(next);
        if(selectedAfter>=-1)selection_=selectedAfter;
        if(std::size_t(std::max(selection_,0))>=scene_.objects.size())selection_=-1;
        dirty_=true;updateAll();
    } catch(std::exception const& e) {
        log("Edit rejected: "+from8(e.what()));
    }
}
void QtEditorWindow::instantiatePrefab(
    QString const& reference,QVector3D position) {
    if(file_.empty()) {
        log("Open or save a project scene before inserting a prefab.");
        return;
    }
    try {
        // Decode and validate Scene V3 before the authoring transaction.
        // Instantiation preserves all descendants, layers, components and
        // relative transforms instead of placing a placeholder model.
        auto const prefab=prefab_preview::load(
            native(folderRoot(file_)),utf8(reference));
        int created=-1;
        editScene([&](Scene& target) {
            editor_tools::promote_to_v3(target);
            created=static_cast<int>(editor_tools::instantiate_prefab(
                target,prefab,{position.x(),position.y(),position.z()}));
        });
        if(created>=0 && static_cast<std::size_t>(created)<scene_.objects.size()) {
            chooseObject(created);
            log(QString("Instantiated prefab %1 (%2 actors)")
                .arg(reference).arg(prefab.objects.size()));
        }
    } catch(std::exception const& error) {
        log("Prefab insertion rejected: "+from8(error.what()));
    }
}

// One authoritative scene placement path for the palette, toolbar and drops.
void QtEditorWindow::placeActorById(QString const& id,QVector3D position) {
    if(!std::isfinite(position.x()) || !std::isfinite(position.y()) ||
       !std::isfinite(position.z())) {
        log("Place Actors: rejected non-finite drop position");
        return;
    }
    auto const countBefore=scene_.objects.size();
    if(id.startsWith("geometry.")) {
        const auto type=id.mid(9);
        NorthStarGeometryRequest request{sizeof(NorthStarGeometryRequest),
                                         NORTHSTAR_GEOMETRY_SOLID,0,0,
                                         0.f,0.f,0,0};
        QString label;
        if(type=="cube")label="Cube";
        else if(type=="cylinder") {label="Cylinder";request.shape=3;}
        else if(type=="ellipsoid") {label="Ellipsoid";request.shape=4;}
        else if(type=="ring") {
            label="Ring";request.kind=NORTHSTAR_GEOMETRY_RING;
            request.a=.65f;request.b=.24f;request.u=24;
        } else if(type=="torus") {
            label="Torus";request.kind=NORTHSTAR_GEOMETRY_TORUS;
            request.a=.65f;request.b=.24f;
            request.u=24;request.v=12;
        }
        if(label.isEmpty()){log("Unknown geometry actor: "+id);return;}
        createProceduralActor(label,request,position);
    } else if(id=="gameplay.player_blueprint") {
        int created=-1;
        editScene([&](Scene& scene) {
            editor_tools::promote_to_v3(scene);
            bool local=true;
            for(auto const& actor:scene.objects)
                if(scene_pawn::is_local(scene,actor)) {local=false;break;}
            auto result=player_blueprint::create(scene,
                local?"PlayerCharacter":"CharacterBlueprint",
                {position.x(),position.y(),position.z()},local);
            for(std::size_t i=0;i<scene.objects.size();++i)
                if(scene.objects[i].node.id==result.pawn)
                    created=static_cast<int>(i);
        });
        if(created>=0 && scene_.objects.size()>countBefore)chooseObject(created);
    } else if(id=="gameplay.character") {
        int created=-1;
        editScene([&](Scene& scene) {
            editor_tools::promote_to_v3(scene);
            if(scene.objects.size()>=max_scene_objects)
                throw std::runtime_error("Scene actor limit reached");
            std::uint64_t maxId=0;
            bool hasPlayer=false;
            for(auto const& actor:scene.objects) {
                maxId=std::max(maxId,actor.node.id);
                hasPlayer|=scene_pawn::is_local(scene,actor);
            }
            if(maxId==std::numeric_limits<std::uint64_t>::max())
                throw std::runtime_error("Scene entity IDs exhausted");
            SceneObject actor;
            actor.name=hasPlayer?"Character":"PlayerCharacter";
            actor.position={position.x(),position.y(),position.z()};
            actor.size=1.8f;actor.color=0x48b8e4u;actor.mass=80.f;
            actor.node.id=maxId+1;
            scene.objects.push_back(std::move(actor));
            player_character::add_character_components(scene,maxId+1,!hasPlayer);
            created=int(scene.objects.size()-1);
        });
        if(created>=0 && scene_.objects.size()>countBefore)chooseObject(created);
    } else {
        SceneComponentBlob component;
        QString label;
        float verticalOffset=0.f;
        if(id=="camera.free") {
            label="CameraActor";verticalOffset=1.6f;
            component.type=scene_camera::type;
            component.version=scene_camera::version;
            component.payload=scene_camera::encode({});
        } else if(id=="light.point" || id=="light.spot") {
            label=id=="light.point"?"PointLight":"SpotLight";
            verticalOffset=2.5f;
            component.type="engine.render.light";
            component.version=1;
            // The current light ABI supports point and spot (not a fake
            // directional-light emulation). Positions are entity-relative.
            component.payload=id=="light.point"?
                "0 0 0 18 1 0.94 0.78 8 0 -1 0 -1":
                "0 0 0 26 1 0.94 0.78 12 0 -1 0 0.82";
        } else if(id.startsWith("component.")) {
            const auto type=id.mid(10).toStdString();
            auto const schema=scene_component_schema(type);
            if(!schema) {log("Component provider unavailable: "+id);return;}
            std::size_t bytes=0;
            for(auto const& field:schema->editor_fields)
                bytes=std::max(bytes,std::size_t(field.offset)+4);
            if(bytes>4096) {
                log("Component default payload exceeds editor limit: "+id);
                return;
            }
            component.type=type;
            component.version=schema->min_version;
            component.payload.assign(bytes,'\0');
            label=id.mid(10).section('.',-1);
        } else {
            log("Unknown placeable actor: "+id);
            return;
        }
        if(!validate_scene_component_blob(component)) {
            log("Provider rejected default component: "+id);
            return;
        }
        int created=-1;
        editScene([&](Scene& scene) {
            editor_tools::promote_to_v3(scene);
            if(scene.objects.size()>=max_scene_objects)
                throw std::runtime_error("Scene actor limit reached");
            std::uint64_t highest=0;
            for(auto const& actor:scene.objects)
                highest=std::max(highest,actor.node.id);
            if(highest==std::numeric_limits<std::uint64_t>::max())
                throw std::runtime_error("Scene entity IDs exhausted");
            SceneObject actor;
            actor.name=label.toStdString();
            actor.position={position.x(),position.y()+verticalOffset,position.z()};
            actor.size=.32f;
            actor.mass=0.f;
            actor.node.id=highest+1;
            actor.node.collision=false;
            component.entity_id=highest+1;
            scene.objects.push_back(std::move(actor));
            scene.components.push_back(component);
            created=int(scene.objects.size()-1);
        });
        if(created>=0 && scene_.objects.size()>countBefore)chooseObject(created);
    }
    if(scene_.objects.size()>countBefore) {
        if(placeActors_)placeActors_->recordPlaced(id);
        log("Place Actors: "+id);
    }
}

void QtEditorWindow::createActor(QString const& asset,QVector3D point) {
    if(!asset.isEmpty()) {
        if(!safe_virtual_path(utf8(asset))) {
            log("Rejected unsafe asset reference: "+asset);return;
        }
        if(!isMesh(asset)) {
            log("Only native YDD/YDR or optional OBJ geometry may be placed.");
            return;
        }
        if(file_.empty()) {
            log("Save or open a project scene before placing file-based assets.");
            return;
        }
        // Engine VFS resolves project-local models before sharedAssets.
        // A pistol YDD may be shared; its real file need not be copied into
        // every project's Content folder.
        auto const fileRef=asset.section('@',0,0);
        auto const local=QDir(folderRoot(file_)).filePath(fileRef);
        auto const shared=QDir(qpath(sharedMaterialRoot())).filePath(fileRef);
        if(!QFileInfo::exists(local) && !QFileInfo::exists(shared)) {
            log("Model absent from project assets and sharedAssets: "+asset);
            return;
        }
    }
    int after=-1;
    editScene([&](Scene& scene) {
        editor_tools::promote_to_v3(scene);
        if(scene.objects.size()>=max_scene_objects)
            throw std::runtime_error("Scene actor limit reached");
        SceneObject o;
        o.name=asset.isEmpty()?"Cube":utf8(QFileInfo(asset).completeBaseName().left(80));
        if(o.name.empty())o.name="Mesh";
        o.position={point.x(),point.y(),point.z()};
        o.mass=0.f;
        o.node.asset=utf8(asset);
        std::uint64_t id=0;
        for(auto const& existing:scene.objects)id=std::max(id,existing.node.id);
        if(id==std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Entity ID range exhausted");
        o.node.id=id+1;
        scene.objects.push_back(std::move(o));
        after=int(scene.objects.size()-1);
    });
    if(after>=0)chooseObject(after);
}
void QtEditorWindow::createProceduralActor(
    QString const& title,NorthStarGeometryRequest request,QVector3D point) {
    int created=-1;
    editScene([&](Scene& scene) {
        editor_tools::promote_to_v3(scene);
        if(scene.objects.size()>=max_scene_objects)
            throw std::runtime_error("Scene actor limit reached");
        SceneObject actor;
        actor.name=utf8(title);
        actor.position={point.x(),point.y(),point.z()};
        actor.mass=0.f;
        actor.node.asset.clear();
        for(auto const& previous:scene.objects)
            actor.node.id=std::max(actor.node.id,previous.node.id);
        if(actor.node.id==std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Entity ID range exhausted");
        ++actor.node.id;
        auto const entity=actor.node.id;
        scene.objects.push_back(std::move(actor));
        scene.components.push_back(
            make_geometry_primitive_component(entity,request));
        created=int(scene.objects.size()-1);
    });
    if(created>=0)chooseObject(created);
}
bool QtEditorWindow::openScene(QString const& file) {
    try {
        auto path=native(file);
        auto next=load_scene(path);
        validate_scene(next);
        if(!file_.empty()) {
            QSettings settings("KaylasSystems","NorthStarQtEditor");
            settings.setValue(sceneCameraKey(file_)+"/LastCamera",
                              cameraValues(viewport_->camera()));
        }
        scene_=std::move(next);
        file_=fs::absolute(path);
        history_.clear();dirty_=false;selection_=-1;
        browser_->setProjectScene(qpath(file_));
        viewport_->setAssetSources(file_.parent_path());
        // Project manifests own the default runtime mode; editor authoring
        // should not silently launch an FPS scene as an ordinary static view.
        QFile projectManifest(QDir(qpath(file_.parent_path())).filePath(
            "northstarcpp.project.json"));
        if(projectManifest.open(QIODevice::ReadOnly)) {
            auto document=QJsonDocument::fromJson(projectManifest.readAll());
            if(document.isObject()) {
                auto mode=document.object().value("game_mode").toString().toLower();
                if(mode=="fps")fpsModeAction_->setChecked(true);
                else if(mode=="explore")exploreModeAction_->setChecked(true);
                else {
                    fpsModeAction_->setChecked(false);
                    exploreModeAction_->setChecked(false);
                }
            }
        }
        QSettings settings("KaylasSystems","NorthStarQtEditor");
        applyCameraValues(viewport_,settings.value(
            sceneCameraKey(file_)+"/LastCamera"));
        updateAll();
        log("Opened "+file);
        return true;
    } catch(std::exception const& e) {
        log("Open scene failed: "+from8(e.what()));
        return false;
    }
}
bool QtEditorWindow::saveScene(QString path) {
    try {
        fs::path target=file_;
        // Explicit programmatic paths must not trigger an interactive dialog.
        if(!path.isEmpty() && path!="*save-as*")target=native(path);
        if(path=="*save-as*" || target.empty()) {
            QFileDialog dialog(this,"Save NorthStar Binary Scene",qpath(target));
            dialog.setNameFilter("NorthStar Binary Scenes (*.nscn);;All Files (*)");
            dialog.setAcceptMode(QFileDialog::AcceptSave);
            dialog.setOption(QFileDialog::DontUseNativeDialog,true);
            if(dialog.exec()!=QDialog::Accepted || dialog.selectedFiles().isEmpty())
                return false;
            target=native(dialog.selectedFiles().front());
        }
        if(target.extension()!=".nscn" && target.extension()!=".nscene")
            target+=fs::path(".nscn");
        save_scene(scene_,target);
        bool changed=file_!=target;
        file_=target;
        dirty_=false;
        if(changed) {
            browser_->setProjectScene(qpath(file_));
            viewport_->setAssetSources(file_.parent_path());
        }
        updateAll();
        log("Saved NSCN binary scene: "+qpath(file_));
        return true;
    } catch(std::exception const& e) {
        log("Save failed: "+from8(e.what()));
        std::fprintf(stderr,"[ERROR] [qt.editor.save] %s code=%d\n",e.what(),dynamic_cast<std::system_error const*>(&e)?dynamic_cast<std::system_error const*>(&e)->code().value():-1);
        return false;
    }
}
void QtEditorWindow::undo() {
    Scene result;
    std::size_t index=0;
    if(history_.undo(scene_,selection_<0?0:std::size_t(selection_),result,index)) {
        scene_=std::move(result);
        selection_=index<scene_.objects.size()?int(index):-1;
        dirty_=true;updateAll();
    }
}
void QtEditorWindow::redo() {
    Scene result;
    std::size_t index=0;
    if(history_.redo(scene_,selection_<0?0:std::size_t(selection_),result,index)) {
        scene_=std::move(result);
        selection_=index<scene_.objects.size()?int(index):-1;
        dirty_=true;updateAll();
    }
}
bool QtEditorWindow::confirmDiscard() {
    if(!dirty_)return true;
    auto response=QMessageBox::question(this,"Unsaved Scene",
        "Save changes before switching scenes?",QMessageBox::Save|
        QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
    if(response==QMessageBox::Cancel)return false;
    return response!=QMessageBox::Save || saveScene();
}
void QtEditorWindow::wireAssetMutation(QtContentBrowser* content) {
    if(!content)return;
    content->setMutationPreflight([this] {
        if(!dirty_)return true;
        auto choice=QMessageBox::question(this,"Save Before Asset Move",
            "Your scene has unsaved changes. Save before rewriting references?",
            QMessageBox::Save|QMessageBox::Cancel,QMessageBox::Cancel);
        return choice==QMessageBox::Save && saveScene();
    });
    connect(content,&QtContentBrowser::assetReferencesRemapped,this,
            [this](QMap<QString,QString> const& changes) {
        bool updated=false;
        for(auto& object:scene_.objects) {
            auto relink=[&](std::string& value) {
                auto old=QString::fromUtf8(value.c_str());
                auto it=changes.constFind(old);
                if(it==changes.cend())return;
                value=it.value().toUtf8().toStdString();
                updated=true;
            };
            relink(object.node.asset);
            relink(object.node.script);
        }
        if(updated) {
            dirty_=true;
            updateAll(); // Sync live editor state with rewritten disk scene.
            log("Asset references updated in the active scene");
        }
    });
}
void QtEditorWindow::showContentBrowser(QString const& virtualReference) {
    if(!contentDock_ || !browser_)return;
    if(dockWidgetArea(contentDock_)==Qt::NoDockWidgetArea)
        addDockWidget(Qt::BottomDockWidgetArea,contentDock_);
    // QMainWindow::restoreState can resurrect an old floating layout even
    // when Floatable was removed from the current dock features.
    if(contentDock_->isFloating())
        contentDock_->setFloating(false);
    contentDock_->show();
    contentDock_->raise();
    if(!virtualReference.isEmpty())
        browser_->revealAsset(virtualReference);
    else browser_->focusSearch();
}

void QtEditorWindow::restoreWorkspace() {
    QSettings settings("KaylasSystems","NorthStarQtEditor");
    auto geometry=settings.value("Workspace/Geometry").toByteArray();
    auto state=settings.value("Workspace/State").toByteArray();
    // QMainWindow can only restore tabified dock positions for docks that
    // already exist. Recreate stable IDs before restoring the saved layout.
    auto const storedTabs=settings.value(
        "Workspace/ContentBrowserTabs").toStringList();
    for(int i=2;i<=9;++i) {
        QString id=QString("contentDockInstance%1").arg(i);
        if(storedTabs.contains(id))createContentBrowserTab(id,false);
    }
    if(!geometry.isEmpty())restoreGeometry(geometry);
    if(!state.isEmpty())restoreState(state,1);
    // Older workspaces may have a floating Content Browser. Re-dock it,
    // preserving the user's explicit choice to hide the panel.
    if(contentDock_ && contentDock_->isFloating()) {
        addDockWidget(Qt::BottomDockWidgetArea,contentDock_);
        contentDock_->setFloating(false);
    }
    // Older layouts may have floated the Inspector over the ContentBrowser.
    // Keep it docked so only the inner form scrolls vertically.
    if(detailsDock_ && detailsDock_->isFloating()) {
        addDockWidget(Qt::RightDockWidgetArea,detailsDock_);
        detailsDock_->setFloating(false);
    }
    configuredPlayerPath_=settings.value("Game/PlayerExecutable").toString();
    if(exploreModeAction_)
        exploreModeAction_->setChecked(settings.value("Game/Explore",false).toBool());
    if(fpsModeAction_ && !exploreModeAction_->isChecked())
        fpsModeAction_->setChecked(settings.value("Game/FPS",false).toBool());
    if(viewport_) {
        viewport_->setFlySpeed(settings.value("Viewport/FlySpeed",12.).toFloat());
        viewport_->setShowGrid(settings.value("Viewport/Grid",true).toBool());
        int const existing=std::clamp(
            settings.value("Viewport/DrawMode",3).toInt(),0,3);
#ifdef _WIN32
        // One-time compatibility upgrade for earlier stored Shaded=0
        // workspaces. Explicit wireframe modes (1/2) remain untouched.
        // Shaded=0 was the implicit historical default, which rendered all
        // prefabs and character assets as cubes even with a GPU available.
        bool const upgraded=settings.value(
            "Viewport/NativeAssetRenderingV2",false).toBool();
        int const preferred=!upgraded && existing==0 ? 3 : existing;
        settings.setValue("Viewport/NativeAssetRenderingV2",true);
        QTimer::singleShot(0,viewport_,[this,preferred] {
            if(!viewport_)return;
            viewport_->setDrawMode(
                static_cast<QtEditorViewport::DrawMode>(preferred));
            if(auto* mode=findChild<QComboBox*>("viewportDrawMode"))
                mode->setCurrentIndex(int(viewport_->drawMode()));
        });
#else
        viewport_->setDrawMode(static_cast<QtEditorViewport::DrawMode>(
            existing==3?0:existing));
#endif
        applyCameraValues(viewport_,settings.value("Viewport/Camera"));
        if(auto* mode=findChild<QComboBox*>("viewportDrawMode"))
            mode->setCurrentIndex(int(viewport_->drawMode()));
        if(auto* grid=findChild<QCheckBox*>("viewportGridToggle"))
            grid->setChecked(viewport_->showGrid());
        if(auto* speed=findChild<QDoubleSpinBox*>("viewportFlySpeed"))
            speed->setValue(viewport_->flySpeed());
    }
}
void QtEditorWindow::persistWorkspace() const {
    QSettings settings("KaylasSystems","NorthStarQtEditor");
    settings.setValue("Workspace/Geometry",saveGeometry());
    QStringList browserTabs;
    for(int i=2;i<=9;++i) {
        QString id=QString("contentDockInstance%1").arg(i);
        if(findChild<QDockWidget*>(id))browserTabs.append(id);
    }
    settings.setValue("Workspace/ContentBrowserTabs",browserTabs);
    settings.setValue("Workspace/State",saveState(1));
    settings.setValue("Viewport/FlySpeed",viewport_->flySpeed());
    settings.setValue("Viewport/Grid",viewport_->showGrid());
    settings.setValue("Viewport/DrawMode",int(viewport_->drawMode()));
    settings.setValue("Viewport/Camera",cameraValues(viewport_->camera()));
    if(!file_.empty())
        settings.setValue(sceneCameraKey(file_)+"/LastCamera",
                          cameraValues(viewport_->camera()));
    settings.setValue("Game/Explore",
                       exploreModeAction_ && exploreModeAction_->isChecked());
    settings.setValue("Game/FPS",
                       fpsModeAction_ && fpsModeAction_->isChecked());
    settings.setValue("Game/PlayerExecutable",configuredPlayerPath_);
}
void QtEditorWindow::closeEvent(QCloseEvent* e) {
    if(textWorkspace_ && !textWorkspace_->confirmCloseAll()) {
        e->ignore();return;
    }
    if(!confirmDiscard()){e->ignore();return;}
    if(playerProcess_ && playerProcess_->state()!=QProcess::NotRunning)
        stopGame();
    // Persist the original Editor layout, never an immersive layout in
    // which all dock panes were temporarily hidden.
    if(playSurface_)endPlayOverlay();
    persistWorkspace();QMainWindow::closeEvent(e);
}
}
