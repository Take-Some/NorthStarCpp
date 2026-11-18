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
#include <QJsonArray>
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
void QtEditorWindow::wireWorkspaceSignals(QPushButton* apply) {
    connect(outlinerSearch_,&QLineEdit::textChanged,this,[this](QString) {
        updateOutliner();
    });
    connect(outliner_,&QWidget::customContextMenuRequested,this,
            [this](QPoint pos) {
        auto* item=outliner_->itemAt(pos);
        auto index=item?item->data(0,Qt::UserRole).toInt():-1;
        QMenu menu(this);
        if(index>=0 && std::size_t(index)<scene_.objects.size()) {
            // A context click on one of N selected rows must not collapse
            // the whole multi-selection (it previously did).
            auto const id=scene_.objects[std::size_t(index)].node.id;
            bool const selected=selectedActorIds_.count(id)!=0;
            chooseObject(index,selected);
            int const count=int(std::max(std::size_t(1),
                                        selectedActorIds_.size()));
            menu.addAction(count>1?"Focus Primary Actor":"Focus Actor",viewport_,
                           &QtEditorViewport::frameSelected);
            auto* rename=menu.addAction("Rename",this,
                                       &QtEditorWindow::renameSelected);
            rename->setEnabled(count==1);
            if(count==1 && selectedPlayerBlueprintPawn())
                menu.addAction("Edit Player Blueprint...",this,
                               &QtEditorWindow::editSelectedPlayerBlueprint);
            menu.addAction(count>1?QString("Duplicate %1 Actors").arg(count):
                           "Duplicate",this,
                           &QtEditorWindow::duplicateSelected);
            menu.addAction(count>1?QString("Delete %1 Actors").arg(count):
                           "Delete Actor",this,
                           &QtEditorWindow::deleteSelected);
            auto* transformMenu=menu.addMenu("Transform");
            transformMenu->addAction("Copy Primary Transform",this,[this] {
                if(selection_<0 || std::size_t(selection_)>=scene_.objects.size())return;
                auto const& o=scene_.objects[std::size_t(selection_)];
                QJsonObject data{
                    {"format","NorthStar.ActorTransform.v1"},
                    {"position",QJsonArray{o.position.x,o.position.y,o.position.z}},
                    {"rotation",QJsonArray{o.node.rotation.x,o.node.rotation.y,o.node.rotation.z}},
                    {"scale",QJsonArray{o.node.scale.x,o.node.scale.y,o.node.scale.z}}
                };
                if(auto* clipboard=QApplication::clipboard())
                    clipboard->setText(QString::fromUtf8(
                        QJsonDocument(data).toJson(QJsonDocument::Compact)));
            });
            transformMenu->addAction("Paste Transform to Selection",this,[this] {
                auto* clipboard=QApplication::clipboard();
                if(!clipboard)return;
                QJsonParseError parseError{};
                auto const data=QJsonDocument::fromJson(
                    clipboard->text().toUtf8(),&parseError);
                auto const obj=data.object();
                if(parseError.error!=QJsonParseError::NoError ||
                   !data.isObject() ||
                   obj.value("format").toString()!="NorthStar.ActorTransform.v1") {
                    statusBar()->showMessage("Clipboard has no NorthStar actor transform",5000);
                    return;
                }
                std::array<std::array<float,3>,3> values{};
                std::array<char const*,3> const names{"position","rotation","scale"};
                std::array<float,3> const minimum{-900000.f,-360000.f,0.01f};
                std::array<float,3> const maximum{900000.f,360000.f,1000.f};
                for(std::size_t i=0;i<names.size();++i) {
                    auto const array=obj.value(names[i]).toArray();
                    if(array.size()!=3) {
                        statusBar()->showMessage("Incomplete transform in clipboard",5000);
                        return;
                    }
                    for(int axis=0;axis<3;++axis) {
                        if(!array.at(axis).isDouble()) {
                            statusBar()->showMessage("Invalid transform component",5000);
                            return;
                        }
                        double const number=array.at(axis).toDouble();
                        if(!std::isfinite(number) || number<minimum[i] ||
                           number>maximum[i]) {
                            statusBar()->showMessage("Transform is outside the editable range",5000);
                            return;
                        }
                        values[i][std::size_t(axis)]=float(number);
                    }
                }
                auto targets=selectedActorIds_;
                if(targets.empty() && selection_>=0 &&
                   std::size_t(selection_)<scene_.objects.size())
                    targets.insert(scene_.objects[std::size_t(selection_)].node.id);
                if(targets.empty())return;
                editScene([targets,values](Scene& scene) {
                    for(auto& o:scene.objects) {
                        if(!targets.count(o.node.id))continue;
                        o.position={values[0][0],values[0][1],values[0][2]};
                        o.node.rotation={values[1][0],values[1][1],values[1][2]};
                        o.node.scale={values[2][0],values[2][1],values[2][2]};
                    }
                });
            });
            auto* selectionMenu=menu.addMenu("Selection");
            selectionMenu->addAction("Select All Actors",this,[this] {
                selectedActorIds_.clear();
                for(auto const& object:scene_.objects)
                    selectedActorIds_.insert(object.node.id);
                if(!scene_.objects.empty())selection_=0;
                updateOutliner();updateInspector();
            });
            selectionMenu->addAction("Include Descendants",this,
                                     &QtEditorWindow::selectChildren);
            selectionMenu->addAction("Copy Selected Actor Names",this,[this] {
                QStringList names;
                for(auto const& actor:scene_.objects)
                    if(selectedActorIds_.count(actor.node.id))
                        names.append(from8(actor.name));
                if(auto* clipboard=QApplication::clipboard())
                    clipboard->setText(names.join("\n"));
            });
            auto* visibilityMenu=menu.addMenu("Visibility");
            visibilityMenu->addAction("Show Selection",this,[this] {
                setSelectedVisibility(true);
            });
            visibilityMenu->addAction("Hide Selection",this,[this] {
                setSelectedVisibility(false);
            });
            visibilityMenu->addSeparator();
            visibilityMenu->addAction("Hide Unselected",this,[this] {
                auto selected=selectedActorIds_;
                if(selected.empty() && selection_>=0)
                    selected.insert(scene_.objects[std::size_t(selection_)].node.id);
                if(selected.empty())return;
                editScene([selected](Scene& scene) {
                    for(auto& actor:scene.objects)
                        actor.node.visible=selected.count(actor.node.id)!=0;
                });
            });
            visibilityMenu->addAction("Show All",this,[this] {
                editScene([](Scene& scene) {
                    for(auto& actor:scene.objects)actor.node.visible=true;
                });
            });
            auto* layerMenu=menu.addMenu("Move to Layer");
            layerMenu->addAction("Default",this,[this] {
                moveSelectedToLayer("Default");
            });
            for(auto const& layer:scene_.layers) {
                auto const name=from8(layer.name);
                if(name=="Default")continue;
                layerMenu->addAction(name,this,[this,name] {
                    moveSelectedToLayer(name);
                });
            }
            menu.addSeparator();
            auto targets=selectedActorIds_;
            if(targets.empty())targets.insert(id);
            menu.addAction("Detach Selection to Root",this,[this,targets] {
                if(std::none_of(scene_.objects.begin(),scene_.objects.end(),
                    [&targets](SceneObject const& actor) {
                        return actor.node.parent && targets.count(actor.node.id);
                    }))return;
                editScene([targets](Scene& scene) {
                    for(auto& actor:scene.objects)
                        if(targets.count(actor.node.id))actor.node.parent=0;
                });
            });
            menu.addAction("Attach Selection to Actor...",this,[this,targets] {
                std::unordered_map<std::uint64_t,std::uint64_t> parents;
                for(auto const& actor:scene_.objects)
                    parents.emplace(actor.node.id,actor.node.parent);
                QStringList names;
                QHash<QString,std::uint64_t> ids;
                for(auto const& actor:scene_.objects) {
                    auto parent=actor.node.id;
                    std::size_t steps=0;
                    while(parent && steps++<parents.size() && !targets.count(parent)) {
                        auto it=parents.find(parent);
                        parent=it==parents.end()?0:it->second;
                    }
                    if(parent)continue;
                    auto const label=QString("%1  [%2]").arg(from8(actor.name)).arg(actor.node.id);
                    names.append(label);
                    ids.insert(label,actor.node.id);
                }
                if(names.isEmpty()) {
                    statusBar()->showMessage("No valid parent actors",3500);
                    return;
                }
                names.sort(Qt::CaseInsensitive);
                bool accepted=false;
                auto const name=QInputDialog::getItem(this,"Attach Selection",
                    "Search by actor name or ID",names,0,true,&accepted);
                if(!accepted)return;
                auto const target=ids.value(name,0);
                auto const chosen=std::find_if(scene_.objects.begin(),scene_.objects.end(),
                    [target](SceneObject const& actor) {return actor.node.id==target;});
                if(!target || chosen==scene_.objects.end()) {
                    statusBar()->showMessage("Select an actor from the list",3500);
                    return;
                }
                auto const layer=chosen->node.layer;
                if(std::none_of(scene_.objects.begin(),scene_.objects.end(),
                    [&targets,target,&layer](SceneObject const& actor) {
                        return targets.count(actor.node.id) &&
                            (actor.node.parent!=target || actor.node.layer!=layer);
                    }))return;
                editScene([targets,target,layer](Scene& scene) {
                    for(auto& actor:scene.objects) {
                        if(!targets.count(actor.node.id))continue;
                        actor.node.parent=target;
                        actor.node.layer=layer;
                    }
                });
            });
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
                outlinerContentValid_=false;
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
    connect(browser_,&QtContentBrowser::uiAssetEditorRequested,this,
            &QtEditorWindow::openUiAsset);
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
        if(!multi && nextCollider.shape==collision_authoring::Shape::ComplexMesh &&
           mass_->value()>0.f && collision_->isChecked()) {
            QMessageBox::warning(this,"Complex Collision",
                "Exact triangle meshes require Mass = 0. Use Auto Convex for moving objects.");
            return;
        }
        if(!multi && !collision_authoring::valid(nextCollider)) {
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
            (!multi && (name!=current.name || reference!=current.node.asset)) ||
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
        if(multi && (changed(current.mass,mass_) || collisionChanged)) {
            for(auto const& object:scene_.objects) {
                if(!targets.count(object.node.id))continue;
                auto const nextMass=changed(current.mass,mass_)?mass_->value():object.mass;
                bool const nextCollision=collisionChanged?collision_->isChecked():object.node.collision;
                if(nextMass>0 && nextCollision &&
                   collision_authoring::for_object(scene_,object).shape==
                       collision_authoring::Shape::ComplexMesh) {
                    QMessageBox::warning(this,"Complex Collision",
                        "Actor "+from8(object.name)+" requires zero mass for exact triangle collision.");
                    updateInspector();
                    return;
                }
            }
        }
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
        if(auto* editor=field->findChild<QLineEdit*>())
            connect(editor,&QLineEdit::textEdited,this,[this,field](QString const&) {
                if(!uiSync_ && field->hasAcceptableInput())inspectorDirtyScalars_.insert(field);
            });
        connect(field,&QDoubleSpinBox::editingFinished,this,commitInspector);
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
} // namespace northstarcpp::editor
