#include "qt_editor_window.hpp"
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
#include <northstarcpp/sky_project_config.hpp>
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
namespace {
QImage const& neutralMaterialPreview() {
    static const QImage image=material_authoring::neutral_sphere_preview(96);
    return image;
}
}
void QtEditorWindow::refreshMaterialChoices(bool force) {
    if(!materialPicker_)return;
    QString const root=file_.empty()?QString{}:folderRoot(file_);
    if(!force && cachedMaterialRoot_==root && materialPicker_->count()>0)
        return;
    // Invalidates any in-flight worker result without blocking the UI.
    const bool directoryChanged=cachedMaterialRoot_!=root;
    if(force || directoryChanged) {
        materialSpheres_.clear();
        ++materialPreviewRevision_;
    }
    auto previous=materialPicker_->currentData().toString();
    cachedMaterialRoot_=root;
    auto const projectMaterials=file_.empty()?QStringList{}:
        material_authoring::list_materials(native(root));
    auto const sharedRoot=sharedMaterialRoot();
    auto const sharedMaterials=sharedRoot.empty()?QStringList{}:
        material_authoring::list_materials(sharedRoot);
    cachedMaterials_=projectMaterials;
    QSignalBlocker block(materialPicker_);
    materialPicker_->clear();
    materialPicker_->addItem("None",QString{});
    static const QIcon placeholder(QPixmap::fromImage(neutralMaterialPreview()));
    auto addOption=[this](QString const& name,QString const& source) {
        auto const loaded=materialSpheres_.value(name);
        materialPicker_->addItem(loaded.isNull()?placeholder:
            QIcon(QPixmap::fromImage(loaded)),source+" · "+name,name);
        materialPicker_->setItemData(materialPicker_->count()-1,
            source+" material: "+name+"\nStudio sphere shows texture and PBR settings",
            Qt::ToolTipRole);
    };
    QSet<QString> projectOverrides;
    projectOverrides.reserve(projectMaterials.size());
    for(auto const& name:projectMaterials) {
        projectOverrides.insert(name);
        addOption(name,"[Project]");
    }
    for(auto const& name:sharedMaterials) {
        if(projectOverrides.contains(name))continue; // VFS project override
        cachedMaterials_.push_back(name);
        addOption(name,"[Shared]");
    }
    auto const matched=materialPicker_->findData(previous);
    materialPicker_->setCurrentIndex(std::max(0,matched));
    queueMaterialPreviews(matched>0?matched:1);
}
void QtEditorWindow::queueMaterialPreviews(int nearIndex) {
    if(!materialPicker_ || file_.empty() || cachedMaterials_.empty())
        return;
    materialPreviewFocus_=std::clamp(nearIndex,1,
        std::max(1,materialPicker_->count()-1));
    if(materialPreviewBusy_)return;
    // Async, bounded demand batches. No material parsing or texture decode
    // occurs on the GUI thread when the dropdown is opened or searched.
    QStringList work;
    auto add=[&](int index) {
        if(index<1 || index>=materialPicker_->count())return;
        auto const ref=materialPicker_->itemData(index).toString();
        if(!ref.isEmpty() && !materialSpheres_.contains(ref) &&
           !work.contains(ref))
            work.push_back(ref);
    };
    add(materialPreviewFocus_);
    for(int d=1;d<=10;++d) {
        add(materialPreviewFocus_+d);
        add(materialPreviewFocus_-d);
    }
    // Populate the first screen even if no item was highlighted yet.
    if(work.size()<12)
        for(int i=1;i<materialPicker_->count() && i<=20;++i)add(i);
    if(work.isEmpty())return;
    materialPreviewBusy_=true;
    auto const focus=materialPreviewFocus_;
    auto const revision=materialPreviewRevision_;
    auto const root=native(cachedMaterialRoot_);
    auto const shared=sharedMaterialRoot();
    auto* watcher=new QFutureWatcher<QVector<QPair<QString,QImage>>>(this);
    connect(watcher,&QFutureWatcher<QVector<QPair<QString,QImage>>>::finished,
            this,[this,watcher,revision,focus] {
        auto const results=watcher->result();
        watcher->deleteLater();
        materialPreviewBusy_=false;
        if(revision!=materialPreviewRevision_) {
            queueMaterialPreviews(materialPreviewFocus_);
            return;
        }
        QSignalBlocker guard(materialPicker_);
        QHash<QString,int> pickerIndex;
        pickerIndex.reserve(materialPicker_->count());
        for(int i=1;i<materialPicker_->count();++i) {
            auto const reference=materialPicker_->itemData(i).toString();
            if(!pickerIndex.contains(reference))
                pickerIndex.insert(reference,i); // preserve findData() first match
        }
        for(auto const& result:results) {
            auto const& ref=result.first;
            auto const& preview=result.second;
            // Missing/unsupported material previews use a neutral studio
            // sphere rather than restarting a failed decode indefinitely.
            auto const& image=preview.isNull()?neutralMaterialPreview():preview;
            materialSpheres_.insert(ref,image);
            auto const index=pickerIndex.value(ref,-1);
            if(index>=1)
                materialPicker_->setItemIcon(index,
                    QIcon(QPixmap::fromImage(image)));
        }
        if(materialPreviewFocus_!=focus)
            queueMaterialPreviews(materialPreviewFocus_);
    });
    watcher->setFuture(QtConcurrent::run([root,shared,work] {
        QVector<QPair<QString,QImage>> results;
        results.reserve(work.size());
        // Sequential per worker: native YMAT texture codecs have not been
        // audited for concurrent decode across multiple workers.
        for(auto const& ref:work) {
            auto const& source=availableMaterial(root,ref)?root:shared;
            results.push_back(qMakePair(ref,source.empty()?QImage{}:
                material_authoring::sphere_preview(source,ref,96)));
        }
        return results;
    }));
}
void QtEditorWindow::assignMaterial(int actor,QString const& reference) {
    if(actor<0 || std::size_t(actor)>=scene_.objects.size())return;
    if(!reference.isEmpty()) {
        if(!material_authoring::is_material(reference)) {
            log("Only materials may be assigned to scene objects");return;
        }
        auto const project=native(folderRoot(file_));
        if(!availableMaterial(project,reference) &&
           !availableMaterial(sharedMaterialRoot(),reference)) {
            log("Material not found in project content or sharedAssets: "+reference);
            return;
        }
    }
    editScene([actor,reference](Scene& next) {
        if(reference.isEmpty())editor_tools::clear_actor_material(
            next,static_cast<std::size_t>(actor));
        else editor_tools::assign_actor_material(next,
            static_cast<std::size_t>(actor),utf8(reference));
    },actor);
    if(!reference.isEmpty())
        log("Assigned material "+reference+" to actor "+
            from8(scene_.objects[std::size_t(actor)].name));
}
void QtEditorWindow::handleAssetDrop(
    QString const& reference,QVector3D position,int hitActor) {
    if(!safe_virtual_path(utf8(reference))) {
        log("Asset drop rejected: unsafe virtual path");return;
    }
    if(material_authoring::is_texture(reference)) {
        if(hitActor<0 || std::size_t(hitActor)>=scene_.objects.size()) {
            statusBar()->showMessage(
                "Drop a texture onto an object to create and assign a material",5000);
            return;
        }
        if(file_.empty()) {
            log("Save the scene into a project before creating a material");return;
        }
        try {
            auto const created=material_authoring::make_material_for_texture(
                native(folderRoot(file_)),reference);
            browser_->refresh();
            refreshMaterialChoices(true);
            assignMaterial(hitActor,created);
            statusBar()->showMessage(
                "Created "+created+" (Base Color: "+reference+")",6500);
        }catch(std::exception const& e) {
            log("Texture-to-material conversion failed: "+from8(e.what()));
        }
    } else if(material_authoring::is_material(reference)) {
        if(hitActor<0 || std::size_t(hitActor)>=scene_.objects.size()) {
            statusBar()->showMessage("Drop a material onto an existing actor",5000);
            return;
        }
        assignMaterial(hitActor,reference);
    } else if(reference.endsWith(".nprefab",Qt::CaseInsensitive)) {
        instantiatePrefab(reference,position);
    } else if(isMesh(reference)) {
        // Surface coordinates are supplied by the engine.collision narrowphase.
        createActor(reference,position);
    } else {
        statusBar()->showMessage(
            "Drop a prefab or mesh to place it, or a material/texture onto an actor",5000);
    }
}
void QtEditorWindow::openMaterialEditor(QString const& reference) {
    if(file_.empty() || reference.isEmpty())return;
    if(material_authoring::is_material_instance(reference)) {
        try {
            auto const root=native(folderRoot(file_));
            if(!availableMaterial(root,reference)) {
                QMessageBox::information(this,"Material Instance",
                    "Shared Material Instances are read-only. Create a project-local instance.");
                return;
            }
            if(material_authoring::edit_material_instance_dialog(
                this,root,reference)) {
                browser_->refresh();
                viewport_->setAssetSources(project_sky::find_root(file_),file_);
                refreshMaterialChoices(true);
                updateInspector();
                viewport_->update();
                log("Saved GPU Material Instance: "+reference);
            }
        }catch(std::exception const& e) {
            QMessageBox::warning(this,"Material Instance",from8(e.what()));
        }
        return;
    }
    if(reference.contains(".ymat@",Qt::CaseInsensitive)) {
        // The Material Editor operates on a selected NEF8 material entry.
        // A library itself remains a browsable Content Browser directory.
        try {
            auto const root=native(folderRoot(file_));
            auto const library=reference.left(reference.indexOf('@'));
            auto const local=root/std::filesystem::u8path(utf8(library));
            auto const shared=sharedMaterialRoot();
            auto const base=std::filesystem::is_regular_file(local)?root:shared;
            (void)openMaterialAssetDialog(this,base,utf8(reference));
            browser_->refresh();
            viewport_->setAssetSources(project_sky::find_root(file_),file_);
            refreshMaterialChoices(true);
            updateInspector();
            viewport_->update();
        }catch(std::exception const& e) {
            QMessageBox::warning(this,"Material Editor",from8(e.what()));
        }
        return;
    }
    if(!reference.endsWith(".ymat",Qt::CaseInsensitive)) {
        openAssetPreviewModal(this,native(folderRoot(file_)),
            utf8(reference),AssetKind::material);
        return;
    }
    namespace ma=material_authoring;
    try {
        auto const root=native(folderRoot(file_));
        if(!availableMaterial(root,reference)) {
            openAssetPreviewModal(this,sharedMaterialRoot(),
                                  utf8(reference),AssetKind::material);
            return;
        }
        QJsonObject original;
        try {
            original=ma::load_descriptor(root,reference);
        }catch(std::exception const&) {
            if(!reference.endsWith(".ymat",Qt::CaseInsensitive))throw;
            // Imported YMAT kind-4 or multi-entry kind-30 libraries have no
            // editable single-material editor payload: inspect safely, do not
            // overwrite their binary library with a synthetic descriptor.
            openAssetPreviewModal(this,root,utf8(reference),AssetKind::material);
            return;
        }
        QDialog dialog(this);
        dialog.setObjectName("northstarMaterialEditor");
        dialog.setWindowTitle("Material Editor — "+reference);
        dialog.resize(1440,900);
        auto* layout=new QVBoxLayout(&dialog);
        auto* tabs=new QTabWidget(&dialog);
        tabs->setObjectName("materialEditorTabs");
        auto* visualWorkspace=new QWidget(tabs);
        auto* visualLayout=new QVBoxLayout(visualWorkspace);
        visualLayout->setContentsMargins(0,0,0,0);
        auto* mainSplit=new QSplitter(Qt::Horizontal,visualWorkspace);
        mainSplit->setObjectName("materialEditorMainSplitter");
        mainSplit->setChildrenCollapsible(false);
        auto* leftSplit=new QSplitter(Qt::Vertical,mainSplit);
        leftSplit->setObjectName("materialEditorPreviewSplitter");
        leftSplit->setChildrenCollapsible(false);
        auto* properties=new QWidget(leftSplit);
        auto* surfaceScroll=new QScrollArea(leftSplit);
        surfaceScroll->setObjectName("materialEditorSurfaceScroll");
        surfaceScroll->setWidgetResizable(true);
        surfaceScroll->setFrameShape(QFrame::NoFrame);
        surfaceScroll->setWidget(properties);
        auto* surfaceLayout=new QVBoxLayout(properties);
        auto* graphEditor=new MaterialGraphEditor(
            original.value("graph").toObject(),mainSplit,root);
        auto* gpuPreview=new MaterialGpuPreview(root,leftSplit);
        gpuPreview->setObjectName("materialEditorLiveGpuPreview");
        properties->setObjectName("materialEditorSurfaceProperties");
        leftSplit->addWidget(gpuPreview);
        leftSplit->addWidget(surfaceScroll);
        leftSplit->setStretchFactor(0,3);
        leftSplit->setStretchFactor(1,2);
        leftSplit->setSizes({520,330});
        mainSplit->addWidget(leftSplit);
        mainSplit->addWidget(graphEditor);
        mainSplit->setStretchFactor(0,2);
        mainSplit->setStretchFactor(1,5);
        mainSplit->setSizes({400,920});
        visualLayout->addWidget(mainSplit,1);
        tabs->addTab(visualWorkspace,"Visual Editor");
        // Properties are docked below the sphere in the visual workspace.
        // Graph and node inspector remain visible beside the preview.
        layout->addWidget(tabs,1);
        // Material Editor has its own workspace; do not reset preferred
        // panel widths and active tab every time an asset is opened.
        QSettings workspace("KaylasSystems","NorthStarQtEditor");
        dialog.restoreGeometry(workspace.value(
            "MaterialEditor/WindowGeometry").toByteArray());
        tabs->setCurrentIndex(std::clamp(
            workspace.value("MaterialEditor/ActiveTab",0).toInt(),0,0));
        connect(&dialog,&QDialog::finished,&dialog,
                [&dialog,tabs](int) {
            QSettings state("KaylasSystems","NorthStarQtEditor");
            state.setValue("MaterialEditor/WindowGeometry",dialog.saveGeometry());
            state.setValue("MaterialEditor/ActiveTab",tabs->currentIndex());
        });
        auto* form=new QFormLayout;
        auto* title=new QLineEdit(original.value("name").toString(),&dialog);
        title->setObjectName("materialName");
        title->setReadOnly(true);
        form->addRow("Material",title);
        auto* shader=new QComboBox(&dialog);
        shader->setObjectName("materialShader");
        shader->addItems({"standard_lit","unlit"});
        shader->setCurrentText(original.value("shader").toString());
        form->addRow("Shader",shader);
        auto* albedo=new QWidget(&dialog);
        auto* colorLayout=new QHBoxLayout(albedo);
        colorLayout->setContentsMargins(0,0,0,0);
        auto* color=new QLineEdit(original.value("base_color").toString(),albedo);
        color->setObjectName("materialBaseColor");
        auto* chooseColor=new QPushButton("Choose...",albedo);
        colorLayout->addWidget(color);
        colorLayout->addWidget(chooseColor);
        connect(chooseColor,&QPushButton::clicked,&dialog,[this,color,&dialog] {
            auto const initial=QColor(color->text());
            auto const next=QColorDialog::getColor(initial,&dialog,
                "Choose Material Base Color",QColorDialog::DontUseNativeDialog);
            if(next.isValid())color->setText(next.name());
        });
        form->addRow("Base Color",albedo);
        auto number=[&](char const* key,QString const& title) {
            auto* spin=new QDoubleSpinBox(&dialog);
            spin->setObjectName("material_"+QString::fromUtf8(key));
            spin->setRange(0,1);
            spin->setDecimals(3);
            spin->setSingleStep(.05);
            spin->setValue(original.value(key).toDouble());
            form->addRow(title,spin);
            return spin;
        };
        auto* metallic=number("metallic","Metallic");
        auto* roughness=number("roughness","Roughness");
        auto const textureNames=ma::list_textures(root);
        struct TextureField{char const* key;QComboBox* widget;};
        std::vector<TextureField> textureFields;
        for(auto const& [key,label]:std::initializer_list<std::pair<char const*,char const*>>{
            {"base_color_texture","Base Color Texture"},
            {"normal_texture","Normal Texture"},
            {"roughness_texture","Roughness Texture"},
            {"metallic_texture","Metallic Texture"},
            {"emissive_texture","Emissive Texture"}}) {
            auto* picker=new QComboBox(&dialog);
            picker->setObjectName(QString("materialSlot_")+key);
            picker->addItem("None",QString{});
            for(auto const& name:textureNames)
                picker->addItem(name,name);
            auto const current=original.value(key).toString();
            int selected=picker->findData(current);
            if(selected<0 && !current.isEmpty()) {
                picker->addItem("Missing: "+current,current);
                selected=picker->count()-1;
            }
            picker->setCurrentIndex(std::max(selected,0));
            form->addRow(label,picker);
            textureFields.push_back({key,picker});
        }
        surfaceLayout->addLayout(form);
        auto* preview=new QLabel(properties);
        preview->setObjectName("materialTexturePreview");
        preview->setMinimumHeight(140);
        preview->setAlignment(Qt::AlignCenter);
        preview->setStyleSheet("background:#18212d;border:1px solid #41546b;");
        surfaceLayout->addWidget(preview);
        auto* textureWarnings=new AssetWarningPanel(properties);
        textureWarnings->setMaximumHeight(155);
        surfaceLayout->addWidget(textureWarnings);
        auto updatePreview=[root,preview,textureWarnings,base=textureFields.front().widget] {
            auto const source=base->currentData().toString();
            if(source.isEmpty()) {
                textureWarnings->setWarnings({});
                preview->setPixmap(QPixmap{});
                preview->setText("No Base Color texture");return;
            }
            try {
                auto path=ma::resolve_existing(root,source);
                QImage image(qpath(path));
                if(image.isNull()) {
                    preview->setPixmap(QPixmap{});
                    preview->setText("No image preview available");
                    textureWarnings->setWarnings({
                        {AssetWarningKind::Other,source,
                         source.endsWith(".ytd",Qt::CaseInsensitive)?
                         "YTD dictionary: select an entry in Content Browser":
                         "The image decoder cannot preview this texture."}});
                }else {
                    textureWarnings->setWarnings({});
                    preview->setText({});
                    preview->setPixmap(QPixmap::fromImage(
                        image.scaled(230,140,Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation)));
                }
            }catch(std::exception const& e) {
                preview->setPixmap(QPixmap{});
                preview->setText("No image preview available");
                textureWarnings->setWarnings({
                    {AssetWarningKind::Texture,source,from8(e.what())}});
            }
        };
        connect(textureFields.front().widget,QOverload<int>::of(&QComboBox::currentIndexChanged),
                &dialog,[updatePreview](int) {updatePreview();});
        updatePreview();
        // All controls feed a throttled, isolated material asset preview.
        // The preview reuses engine.render and never overwrites project assets.
        auto editedDescriptor=[&]() {
            QJsonObject edited=original;
            edited.insert("shader",shader->currentText());
            edited.insert("base_color",color->text().trimmed());
            edited.insert("metallic",metallic->value());
            edited.insert("roughness",roughness->value());
            for(auto const& item:textureFields)
                edited.insert(item.key,item.widget->currentData().toString());
            if(graphEditor->enabled())edited.insert("graph",graphEditor->graph());
            else edited.remove("graph");
            return edited;
        };
        auto updateGpu=[&](){gpuPreview->setMaterial(editedDescriptor());};
        connect(graphEditor,&MaterialGraphEditor::graphChanged,
                &dialog,updateGpu);
        connect(gpuPreview,&MaterialGpuPreview::shaderDiagnostic,
                graphEditor,&MaterialGraphEditor::showDiagnostic);
        connect(shader,QOverload<int>::of(&QComboBox::currentIndexChanged),
                &dialog,[updateGpu](int){updateGpu();});
        connect(color,&QLineEdit::textChanged,&dialog,
                [updateGpu](QString const&){updateGpu();});
        connect(metallic,QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                &dialog,[updateGpu](double){updateGpu();});
        connect(roughness,QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                &dialog,[updateGpu](double){updateGpu();});
        for(auto const& item:textureFields)
            connect(item.widget,QOverload<int>::of(&QComboBox::currentIndexChanged),
                    &dialog,[updateGpu](int){updateGpu();});
        updateGpu();
        QTimer::singleShot(0,&dialog,[gpuPreview]{gpuPreview->start();});
        auto* buttons=new QDialogButtonBox(
            QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(buttons->button(QDialogButtonBox::Save),&QPushButton::clicked,
                &dialog,[&,root] {
            QJsonObject edited=editedDescriptor();
            QString graphError;
            if(!graphEditor->validate(&graphError)) {
                tabs->setCurrentWidget(graphEditor);
                QMessageBox::warning(&dialog,"Material Graph rejected",graphError);
                return;
            }
            if(graphEditor->enabled())edited.insert("graph",graphEditor->graph());
            else edited.remove("graph");
            try {
                ma::save_descriptor(root,reference,edited);
                dialog.accept();
            }catch(std::exception const& e) {
                QMessageBox::warning(&dialog,"Material rejected",from8(e.what()));
            }
        });
        if(dialog.exec()==QDialog::Accepted) {
            browser_->refresh();
            // Re-resolve the VFS and invalidate cached material texels and
            // GPU draws. The next editor frame uses the newly saved PBR
            // factors/textures rather than the old cached material.
            viewport_->setAssetSources(project_sky::find_root(file_),file_);
            refreshMaterialChoices(true);
            updateInspector();
            viewport_->update();
            log("Saved and reloaded GPU material: "+reference);
        }
    }catch(std::exception const& e) {
        QMessageBox::warning(this,"Material Editor",from8(e.what()));
    }
}
void QtEditorWindow::createMaterial() {
    if(file_.empty()) {
        QMessageBox::information(this,"Project Required",
            "Save or open a scene to create a project-local material.");
        return;
    }
    bool accepted=false;
    auto name=QInputDialog::getText(this,"New Material",
        "Material name:",QLineEdit::Normal,"NewMaterial",&accepted).trimmed();
    if(!accepted)return;
    if(name.isEmpty() || name.size()>80 ||
       name.contains('/') || name.contains('\\') ||
       name.contains(':') || name=="." || name=="..") {
        QMessageBox::warning(this,"Invalid Material","Choose a safe material name.");
        return;
    }
    QDir dir(folderRoot(file_));
    if(!dir.mkpath("Materials")) {
        log("Failed to create Materials directory");return;
    }
    auto material=dir.filePath("Materials/"+name+".ymat");
    if(QFileInfo::exists(material)) {
        QMessageBox::warning(this,"Existing Material","A material with that name exists.");
        return;
    }
    QJsonObject attributes{
        {"schema",1},{"name",name},{"shader","standard_lit"},
        {"base_color","#d4dbe5"},{"metallic",0.0},
        {"roughness",0.65},{"base_color_texture",""}};
    try {
        material_authoring::save_descriptor(native(folderRoot(file_)),
            "Materials/"+name+".ymat",attributes);
    }catch(std::exception const& e) {
        QMessageBox::warning(this,"Create Material",from8(e.what()));
        return;
    }
    browser_->refresh();
    refreshMaterialChoices(true);
    updateInspector();
    log("Created native XML YMAT: "+material);
}
}
