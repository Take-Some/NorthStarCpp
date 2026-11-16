// Prefab collider editing with native viewport diagnostics and atomic save.
#pragma once
#include "qt_prefab_preview.hpp"
#include "qt_editor_viewport.hpp"
#include <northstarcpp/collision_authoring.hpp>
#include <northstarcpp/collision_mesh.hpp>
#include <northstarcpp/collision_hulls.hpp>
#include "qt_collision_cook_task.hpp"
#include "qt_collision_project_settings.hpp"
#include "qt_collision_manage_channels.hpp"
#include <QInputDialog>
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QLabel>
#include <QSaveFile>
#include <QSignalBlocker>
#include <array>
namespace northstarcpp::editor {
inline int openPrefabCollisionEditor(QWidget* parent,std::filesystem::path const& root,
                                     std::string const& ref) {
    Scene prefab=prefab_preview::load(root,ref);
    QDialog dialog(parent);
    dialog.setObjectName("prefabCollisionEditor");
    dialog.setWindowTitle("Prefab Collision - "+QString::fromStdString(ref));
    dialog.resize(1000,670);
    auto* split=new QHBoxLayout(&dialog);
    auto* view=new QtEditorViewport(&dialog);
    view->setObjectName("prefabCollisionViewport");
    view->setScene(&prefab);
    view->setAssetSources(root.parent_path());
    view->setShowCollisions(true);
    view->setDrawMode(QtEditorViewport::DrawMode::EngineGpu);
    split->addWidget(view,4);
    auto* side=new QWidget(&dialog);
    auto* vertical=new QVBoxLayout(side);
    auto* form=new QFormLayout;
    auto* actor=new QComboBox(side);
    actor->setObjectName("prefabCollisionActor");
    for(auto const& o:prefab.objects)
        actor->addItem(QString::fromStdString(o.name));
    form->addRow("Prefab actor",actor);
    auto* enabled=new QCheckBox("Enable collision",side);
    enabled->setObjectName("prefabCollisionEnabled");
    form->addRow(enabled);
    auto* shape=new QComboBox(side);
    shape->setObjectName("prefabCollisionShape");
    shape->addItems({"Box","Sphere","Capsule (Y)",
                     "Complex Mesh (exact, static)","Auto Convex"});
    form->addRow("Shape",shape);
    auto* objectType=new QComboBox(side);
    objectType->setObjectName("prefabCollisionObjectType");
    auto projectSettings=collision_project::load(root.parent_path());
    for(auto const& name:projectSettings.names)objectType->addItem(name);
    form->addRow("Object Type",objectType);
    std::array<QComboBox*,16> responses{};
    auto* responsePanel=new QGroupBox("Collision Response Matrix",side);
    responsePanel->setObjectName("prefabCollisionResponseMatrix");
    auto* responseForm=new QFormLayout(responsePanel);
    for(std::size_t i=0;i<16;++i) {
        auto* entry=new QComboBox(responsePanel);
        entry->setObjectName(QString("prefabResponse_%1")
            .arg(projectSettings.names[i]));
        entry->addItems({"Ignore","Overlap","Block"});
        entry->setCurrentIndex(2);
        responseForm->addRow(projectSettings.names[i],entry);
        responses[i]=entry;
    }
    form->addRow(responsePanel);
    auto* preset=new QComboBox(side);
    preset->setObjectName("prefabCollisionPreset");
    preset->addItem("Custom");
    for(auto const& item:projectSettings.presets)preset->addItem(item.name);
    form->addRow("Collision Preset",preset);
    auto* savePreset=new QPushButton("Save Current As Preset...",side);
    savePreset->setObjectName("prefabSaveCollisionPreset");
    form->addRow(savePreset);
    auto* editChannels=new QPushButton("Edit Project Channels...",side);
    editChannels->setObjectName("prefabEditCollisionChannels");
    form->addRow(editChannels);
    auto* splits=new QSpinBox(side);
    splits->setObjectName("prefabCollisionSplitsNum");
    splits->setRange(1,16);
    splits->setValue(4);
    form->addRow("splitsNum (Auto Convex)",splits);
    auto* hullPreview=new QComboBox(side);
    hullPreview->setObjectName("prefabCollisionHullPreview");
    hullPreview->addItem("All cooked convex hulls");
    form->addRow("Preview Hull",hullPreview);
    std::array<QDoubleSpinBox*,6> numbers{};
    for(int i=0;i<6;++i) {
        auto* value=new QDoubleSpinBox(side);
        value->setRange(i<3?.01:-10000,10000);
        value->setDecimals(3);
        value->setSingleStep(.1);
        numbers[i]=value;
        form->addRow(QString(i<3?"Size ":"Offset ")+QString("XYZ"[i%3]),value);
    }
    vertical->addLayout(form);
    QObject::connect(hullPreview,QOverload<int>::of(&QComboBox::currentIndexChanged),
        &dialog,[view](int index){
            view->setCollisionHullPreview(index-1);
            view->setDrawMode(index>0?QtEditorViewport::DrawMode::ShadedWireframe:
                                        QtEditorViewport::DrawMode::EngineGpu);
        });
    auto* wire=new QCheckBox("Show Collision Wireframe",side);
    wire->setObjectName("prefabCollisionWireframe");
    wire->setChecked(true);
    QObject::connect(wire,&QCheckBox::toggled,view,
                     &QtEditorViewport::setShowCollisions);
    vertical->addWidget(wire);
    auto* apply=new QPushButton("Apply Collision",side);
    apply->setObjectName("prefabCollisionApply");
    vertical->addWidget(apply);
    auto* generate=new QPushButton("Generate Collision From Mesh",side);
    generate->setObjectName("prefabCollisionGenerate");
    vertical->addWidget(generate);
    QObject::connect(shape,QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &dialog,[splits,generate](int i) {
        splits->setEnabled(i==4);
        generate->setEnabled(i==3 || i==4);
    });
    vertical->addStretch();
    auto* save=new QPushButton("Save Prefab",side);
    save->setObjectName("prefabCollisionSave");
    vertical->addWidget(save);
    auto* close=new QPushButton("Cancel",side);
    QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
    vertical->addWidget(close);
    auto* sideScroll=new QScrollArea(&dialog);
    sideScroll->setObjectName("prefabCollisionPropertiesScroll");
    sideScroll->setWidgetResizable(true);
    sideScroll->setMinimumWidth(340);
    sideScroll->setWidget(side);
    split->addWidget(sideScroll,2);
    auto refresh=[&](int i) {
        if(i<0||std::size_t(i)>=prefab.objects.size())return;
        auto const& o=prefab.objects[std::size_t(i)];
        auto const c=collision_authoring::for_object(prefab,o);
        enabled->setChecked(o.node.collision);
        shape->setCurrentIndex(int(c.shape));
        objectType->setCurrentIndex(int(c.object_type));
        {
            QSignalBlocker blocked(preset);
            auto index=preset->findText(QString::fromStdString(c.preset));
            preset->setCurrentIndex(index<0?0:index);
        }
        for(std::size_t k=0;k<16;++k) {
            QSignalBlocker blocked(responses[k]);
            responses[k]->setCurrentIndex(int(c.responses[k]));
        }
        splits->setValue(int(c.splits_num));
        hullPreview->clear();
        hullPreview->addItem("All cooked convex hulls");
        if(auto baked=collision_hulls::lookup(prefab,o.node.id))
            for(std::size_t h=0;h<baked->hulls.size();++h)
                hullPreview->addItem(QString("Hull %1 (%2 vertices)")
                    .arg(h+1).arg(baked->hulls[h].xyz.size()/3));
        float const values[6]{c.dimensions.x,c.dimensions.y,c.dimensions.z,
                              c.offset.x,c.offset.y,c.offset.z};
        for(int k=0;k<6;++k)numbers[k]->setValue(values[k]);
        view->setSelected(i);view->frameSelected();
    };
    auto commit=[&] {
        int const i=actor->currentIndex();
        if(i<0||std::size_t(i)>=prefab.objects.size())return false;
        collision_authoring::Collider c;
        c.shape=static_cast<collision_authoring::Shape>(shape->currentIndex());
        c.dimensions={float(numbers[0]->value()),float(numbers[1]->value()),
                      float(numbers[2]->value())};
        c.offset={float(numbers[3]->value()),float(numbers[4]->value()),
                  float(numbers[5]->value())};
        c.splits_num=std::uint32_t(splits->value());
        c.object_type=static_cast<collision_authoring::ObjectType>(objectType->currentIndex());
        c.preset=preset->currentIndex()>0?preset->currentText().toStdString():"Custom";
        for(std::size_t k=0;k<16;++k)
            c.responses[k]=static_cast<collision_authoring::Response>(
                responses[k]->currentIndex());
        if(c.shape==collision_authoring::Shape::ComplexMesh &&
           prefab.objects[std::size_t(i)].mass>0.f && enabled->isChecked()) {
            QMessageBox::warning(&dialog,"Collision",
                "Complex Mesh requires Mass = 0. Select Auto Convex for dynamic actors.");
            return false;
        }
        if(!collision_authoring::valid(c)) {
            QMessageBox::warning(&dialog,"Collision",
                "Invalid dimensions: capsule Y height must be >= X diameter.");
            return false;
        }
        auto updated=prefab;
        auto& o=updated.objects[std::size_t(i)];
        o.node.collision=enabled->isChecked();
        collision_authoring::assign(updated,o.node.id,c);
        try {validate_scene(updated);}
        catch(std::exception const& e) {
            QMessageBox::warning(&dialog,"Collision",e.what());
            return false;
        }
        prefab=std::move(updated);
        view->setScene(&prefab);
        return true;
    };
    QObject::connect(preset,QOverload<int>::of(&QComboBox::currentIndexChanged),
        &dialog,[&](int index) {
            if(index<=0)return;
            auto key=preset->currentText();
            auto it=std::find_if(projectSettings.presets.begin(),
                projectSettings.presets.end(),
                [&](collision_project::Preset const& item){return item.name==key;});
            if(it==projectSettings.presets.end())return;
            {
                QSignalBlocker blocked(objectType);
                objectType->setCurrentIndex(int(it->channel));
            }
            for(int i=0;i<16;++i) {
                QSignalBlocker blocked(responses[i]);
                responses[i]->setCurrentIndex(int(it->responses[i]));
            }
            commit();
        });
    auto markCustom=[&] {
        QSignalBlocker blocked(preset);
        preset->setCurrentIndex(0);
    };
    QObject::connect(objectType,QOverload<int>::of(&QComboBox::currentIndexChanged),
        &dialog,[markCustom](int){markCustom();});
    for(auto* field:responses)
        QObject::connect(field,QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog,[markCustom](int){markCustom();});
    QObject::connect(savePreset,&QPushButton::clicked,&dialog,[&] {
        bool ok=false;
        auto name=QInputDialog::getText(&dialog,"Save Collision Preset",
            "Preset name:",QLineEdit::Normal,"NewPreset",&ok).trimmed();
        if(!ok)return;
        if(!collision_project::valid_name(name)) {
            QMessageBox::warning(&dialog,"Collision Presets","Invalid preset name");
            return;
        }
        try {
            collision_project::Preset entry;
            entry.name=name;
            entry.channel=std::uint32_t(objectType->currentIndex());
            for(int i=0;i<16;++i)
                entry.responses[i]=static_cast<collision_authoring::Response>(
                    responses[i]->currentIndex());
            auto it=std::find_if(projectSettings.presets.begin(),
                projectSettings.presets.end(),
                [&](collision_project::Preset const& p){return p.name==name;});
            if(it==projectSettings.presets.end())
                projectSettings.presets.push_back(entry);
            else *it=entry;
            collision_project::save(root.parent_path(),projectSettings);
            {
                QSignalBlocker blocked(preset);
                if(preset->findText(name)<0)preset->addItem(name);
                preset->setCurrentIndex(preset->findText(name));
            }
            commit();
        }catch(std::exception const& ex) {
            QMessageBox::warning(&dialog,"Collision Presets",ex.what());
        }
    });
    QObject::connect(editChannels,&QPushButton::clicked,&dialog,[&] {
        if(!collision_project::edit_names(&dialog,root.parent_path()))return;
        projectSettings=collision_project::load(root.parent_path());
        for(int i=0;i<16;++i) {
            objectType->setItemText(i,projectSettings.names[i]);
            if(auto* label=qobject_cast<QLabel*>(
                   responseForm->labelForField(responses[i])))
                label->setText(projectSettings.names[i]);
        }
    });
    QObject::connect(actor,QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &dialog,refresh);
    QObject::connect(apply,&QPushButton::clicked,&dialog,[&]{commit();});
    QObject::connect(generate,&QPushButton::clicked,&dialog,[&] {
        int const index=actor->currentIndex();
        if(index<0 || std::size_t(index)>=prefab.objects.size() ||
           (shape->currentIndex()!=3 && shape->currentIndex()!=4))return;
        if(!commit())return;
        auto const& selected=prefab.objects[std::size_t(index)];
        if(selected.node.asset.empty()) {
            QMessageBox::warning(&dialog,"Collision",
                "Assign an OBJ/YDD/YDR model to this prefab actor first.");
            return;
        }
        try {
            AssetStore assets;
            assets.mount_project_assets(root.parent_path());
            auto baked=collision_mesh::bake(assets,selected.node.asset);
            auto const entity=selected.node.id;
            auto const source=baked.source;
            auto apply_baked=[&,entity,source,baked](std::optional<collision_hulls::Set> hulls) {
                auto found=std::find_if(prefab.objects.begin(),prefab.objects.end(),
                    [entity](SceneObject const& o){return o.node.id==entity;});
                if(found==prefab.objects.end()||found->node.asset!=source)return;
                auto candidate=prefab;
                collision_mesh::assign(candidate,entity,baked);
                if(hulls)collision_hulls::assign(candidate,entity,*hulls);
                validate_scene(candidate);
                prefab=std::move(candidate);
                view->setScene(&prefab);
                refresh(actor->currentIndex());
            };
            if(shape->currentIndex()==4) {
                startCollisionCook(&dialog,std::move(baked),
                    std::uint32_t(splits->value()),
                    root.parent_path()/".northstar"/"cache"/"collisions",
                    [apply_baked](collision_hulls::Set hulls,bool) mutable {
                        apply_baked(std::move(hulls));
                    },generate);
            } else apply_baked(std::nullopt);
        }catch(std::exception const& e) {
            QMessageBox::warning(&dialog,"Collision Generator",
                QString::fromUtf8(e.what()));
        }
    });
    QObject::connect(save,&QPushButton::clicked,&dialog,[&] {
        if(!commit())return;
        for(auto const& object:prefab.objects) {
            if(!object.node.collision)continue;
            auto const c=collision_authoring::for_object(prefab,object);
            if(c.shape!=collision_authoring::Shape::ComplexMesh &&
               c.shape!=collision_authoring::Shape::AutoConvex)continue;
            auto mesh=collision_mesh::lookup(prefab,object.node.id);
            auto hulls=collision_hulls::lookup(prefab,object.node.id);
            if(!mesh || mesh->source!=object.node.asset ||
               (c.shape==collision_authoring::Shape::AutoConvex &&
                (!hulls || hulls->source!=object.node.asset ||
                 hulls->splits_num!=c.splits_num))) {
                QMessageBox::warning(&dialog,"Prefab Collision",
                    "Generate collision geometry before saving a mesh collider.");
                return;
            }
        }
        auto const path=prefab_preview::source(root,ref);
        auto const text=serialize_scene_v3(prefab);
        if(text.size()>4*1024*1024) {
            QMessageBox::warning(&dialog,"Prefab Collision",
                "Collision geometry exceeds the 4 MiB prefab asset limit.");
            return;
        }
        QSaveFile output(QString::fromStdWString(path.wstring()));
        if(!output.open(QIODevice::WriteOnly) ||
           output.write(text.data(),qsizetype(text.size()))!=qsizetype(text.size()) ||
           !output.commit()) {
            QMessageBox::warning(&dialog,"Prefab","Atomic save failed");
            return;
        }
        dialog.accept();
    });
    refresh(0);
    return dialog.exec();
}
} // namespace northstarcpp::editor
