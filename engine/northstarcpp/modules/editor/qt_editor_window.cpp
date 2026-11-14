#include "qt_editor_window.hpp"
#include "qt_editor_viewport.hpp"
#include "qt_content_browser.hpp"
#include <northstarcpp/editor_asset_catalog.hpp>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
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
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace northstarcpp::editor {
namespace fs=std::filesystem;
namespace {
std::string utf8(QString const& value) {return value.toUtf8().toStdString();}
QString from8(std::string const& value) {return QString::fromUtf8(value.data(),int(value.size()));}
fs::path native(QString const& value) {return fs::u8path(utf8(value));}
QString qpath(fs::path const& value) {return QString::fromUtf8(value.u8string().c_str());}
QString folderRoot(fs::path const& scene) {
    return qpath(scene.parent_path()/"assets");
}
bool isMesh(QString const& path) {
    auto ext=QFileInfo(path).suffix().toLower();
    return QStringList{"ydd","obj","mesh","fbx","gltf","glb"}.contains(ext);
}
QDoubleSpinBox* scalar(QWidget* parent,double lo,double hi,int decimals=3) {
    auto* box=new QDoubleSpinBox(parent);
    box->setRange(lo,hi);
    box->setDecimals(decimals);
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
    editor_tools::promote_to_v3(scene_);
    setupUi();
    restoreWorkspace();
    updateAll();
}
QString QtEditorWindow::sceneTitle() const {
    return QString("NorthStar Editor — %1%2")
        .arg(file_.empty()?"Untitled":qpath(file_.filename()),dirty_?" *":"");
}
void QtEditorWindow::setupUi() {
    setStyleSheet(R"QSS(
QMainWindow,QDockWidget {background:#151c26;color:#d6e4f2;}
QDockWidget {titlebar-close-icon:none;titlebar-normal-icon:none;}
QDockWidget::title {background:#263446;padding:6px 8px;border:1px solid #34485d;}
QTreeWidget,QLineEdit,QDoubleSpinBox,QPlainTextEdit {
 background:#192331;border:1px solid #36495e;color:#e1ecf7;
 selection-background-color:#356588;border-radius:3px;padding:3px;
}
QTreeWidget::item {padding:4px;}
QTreeWidget::item:selected {background:#366085;}
QLabel {color:#acbed1;}
QPushButton,QToolButton {
 background:#293e51;border:1px solid #45607c;
 color:#e0e9f6;padding:5px;border-radius:4px;
}
QPushButton:hover,QToolButton:hover {background:#386180;}
QMenuBar,QMenu,QStatusBar {background:#1d2a3a;color:#e1ebf4;}
QToolBar {background:#202c3d;border:0;spacing:6px;}
QCheckBox {color:#dce7f5;}
)QSS");
    viewport_=new QtEditorViewport(this);
    setCentralWidget(viewport_);
    auto* toolbar=addToolBar("Authoring");
    toolbar->setObjectName("authoringToolbar");
    toolbar->setMovable(true);
    auto* fileMenu=menuBar()->addMenu("&File");
    auto* editMenu=menuBar()->addMenu("&Edit");
    auto* viewMenu=menuBar()->addMenu("&View");
    auto* assetMenu=menuBar()->addMenu("&Assets");
    auto* newScene=fileMenu->addAction("&New Scene",QKeySequence::New,[this]{
        if(!confirmDiscard())return;
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
    auto* add=editMenu->addAction("Add &Primitive",QKeySequence(Qt::CTRL|Qt::Key_Insert),
        [this]{createActor({},{0,0,0});});
    auto* clone=editMenu->addAction("&Duplicate Object",QKeySequence(Qt::CTRL|Qt::Key_D),
        [this] {
            if(selection_<0)return;
            try {
                int next=selection_;
                editScene([&](Scene& scene){
                    next=int(editor_tools::duplicate(scene,std::size_t(selection_)));
                },-2);
                chooseObject(next);
            } catch(std::exception const& e){log(from8(e.what()));}
        });
    auto* remove=editMenu->addAction("Delete Object",QKeySequence::Delete,
        [this] {
            if(selection_<0)return;
            editScene([this](Scene& scene){
                editor_tools::delete_subtree(scene,std::size_t(selection_));
            });
        });
    assetMenu->addAction("&New Material Descriptor...",[this]{createMaterial();});
    assetMenu->addAction("&Refresh Content",QKeySequence(Qt::Key_F5),[this]{
        browser_->refresh();
        updateDependencies();
    });

    auto* modeMove=toolbar->addAction("W Translate");
    auto* modeRotate=toolbar->addAction("E Rotate");
    auto* modeScale=toolbar->addAction("R Scale");
    modeMove->setShortcut(QKeySequence(Qt::Key_W));
    modeRotate->setShortcut(QKeySequence(Qt::Key_E));
    modeScale->setShortcut(QKeySequence(Qt::Key_R));
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
    toolbar->addSeparator();
    toolbar->addAction("Focus Selected",QKeySequence(Qt::Key_F),viewport_,
                       &QtEditorViewport::frameSelected);
    toolbar->addSeparator();
    auto* snap=new QCheckBox("Snap",this);
    snap->setChecked(true);
    connect(snap,&QCheckBox::toggled,this,[this](bool checked){
        gizmoOptions_.snap=checked;
    });
    toolbar->addWidget(snap);
    toolbar->addSeparator();
    toolbar->addAction("Add Cube",add->shortcut(),[this]{createActor({},{0,0,0});});
    toolbar->addAction("Save",this,[this]{saveScene();});

    outlinerDock_=new QDockWidget("World Outliner",this);
    outlinerDock_->setObjectName("worldOutlinerDock");
    auto* left=new QWidget(outlinerDock_);
    auto* leftLayout=new QVBoxLayout(left);
    leftLayout->setContentsMargins(5,5,5,5);
    outlinerSearch_=new QLineEdit(left);
    outlinerSearch_->setPlaceholderText("Search actors...");
    leftLayout->addWidget(outlinerSearch_);
    outliner_=new QTreeWidget(left);
    outliner_->setObjectName("actorTree");
    outliner_->setHeaderLabels({"Actor","Type"});
    outliner_->setAlternatingRowColors(true);
    outliner_->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(outliner_);
    outlinerDock_->setWidget(left);
    addDockWidget(Qt::LeftDockWidgetArea,outlinerDock_);

    detailsDock_=new QDockWidget("Details | Properties",this);
    detailsDock_->setObjectName("inspectorDock");
    auto* details=new QWidget(detailsDock_);
    auto* form=new QFormLayout(details);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setContentsMargins(12,12,12,12);
    form->setSpacing(8);
    objectName_=new QLineEdit(details);
    assetPath_=new QLineEdit(details);
    assetPath_->setPlaceholderText("Project-relative mesh reference");
    form->addRow("Actor Name",objectName_);
    form->addRow("Asset Reference",assetPath_);
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
    auto* apply=new QPushButton("Apply Object Properties",details);
    form->addRow(apply);
    auto* hint=new QLabel("Double-click actor to select. Drag mesh from "
        "Content Browser to the viewport. W/E/R change gizmo mode.",details);
    hint->setWordWrap(true);
    form->addRow(hint);
    detailsDock_->setWidget(details);
    addDockWidget(Qt::RightDockWidgetArea,detailsDock_);

    contentDock_=new QDockWidget("Content Browser",this);
    contentDock_->setObjectName("contentDock");
    browser_=new QtContentBrowser(contentDock_);
    contentDock_->setWidget(browser_);
    addDockWidget(Qt::BottomDockWidgetArea,contentDock_);
    contentDock_->setMinimumHeight(220);

    dependencyDock_=new QDockWidget("Asset References",this);
    dependencyDock_->setObjectName("dependencyDock");
    dependencies_=new QTreeWidget(dependencyDock_);
    dependencies_->setHeaderLabels({"Reference","Status"});
    dependencies_->setObjectName("assetReferenceTree");
    dependencyDock_->setWidget(dependencies_);
    addDockWidget(Qt::RightDockWidgetArea,dependencyDock_);
    tabifyDockWidget(detailsDock_,dependencyDock_);
    detailsDock_->raise();

    auto* output=new QDockWidget("Output Log",this);
    output->setObjectName("editorOutputDock");
    messages_=new QPlainTextEdit(output);
    messages_->setReadOnly(true);
    messages_->setMaximumBlockCount(1000);
    output->setWidget(messages_);
    addDockWidget(Qt::BottomDockWidgetArea,output);
    tabifyDockWidget(contentDock_,output);
    contentDock_->raise();

    for(auto* dock:{outlinerDock_,detailsDock_,contentDock_,dependencyDock_,output})
        viewMenu->addAction(dock->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction("Reset Workspace Layout",[this] {
        resizeDocks({outlinerDock_,detailsDock_},{290,355},Qt::Horizontal);
        resizeDocks({contentDock_},{300},Qt::Vertical);
        for(auto* dock:{outlinerDock_,detailsDock_,contentDock_,dependencyDock_})
            dock->show();
        contentDock_->raise();detailsDock_->raise();
    });

    connect(outlinerSearch_,&QLineEdit::textChanged,this,[this](QString){
        updateOutliner();
    });
    connect(outliner_,&QTreeWidget::currentItemChanged,this,
            [this](QTreeWidgetItem* item,QTreeWidgetItem*) {
        if(!uiSync_)chooseObject(item?item->data(0,Qt::UserRole).toInt():-1);
    });
    connect(viewport_,&QtEditorViewport::selectionChanged,this,
            &QtEditorWindow::chooseObject);
    connect(viewport_,&QtEditorViewport::assetDropped,this,
            &QtEditorWindow::createActor);
    connect(viewport_,&QtEditorViewport::gizmoDelta,this,
            [this](int mode,int axis,float amount) {
        if(selection_<0)return;
        try {
            editScene([=](Scene& scene) {
                editor_tools::apply_gizmo(scene,std::size_t(selection_),
                    static_cast<editor_tools::GizmoMode>(mode),
                    static_cast<editor_tools::GizmoAxis>(axis),amount,
                    gizmoOptions_);
            });
        } catch(std::exception const& e){log(from8(e.what()));}
    });
    connect(browser_,&QtContentBrowser::assetActivated,this,
            [this](QString ref){
        if(isMesh(ref))createActor(ref,{0,0,0});
        else log("Selected asset: "+ref);
    });
    connect(browser_,&QtContentBrowser::assetSelected,this,
            [this](QString ref){if(!ref.isEmpty())
                statusBar()->showMessage("Content / "+ref,2500);});
    connect(apply,&QPushButton::clicked,this,[this] {
        if(selection_<0)return;
        auto name=utf8(objectName_->text().trimmed());
        auto reference=utf8(assetPath_->text().trimmed());
        if(!reference.empty() && !safe_virtual_path(reference)) {
            QMessageBox::warning(this,"Invalid Reference",
                "Asset reference must be project-relative and contain no traversal.");
            return;
        }
        editScene([&](Scene& scene) {
            auto& object=scene.objects[std::size_t(selection_)];
            object.name=name;
            object.node.asset=reference;
            object.position={float(px_->value()),float(py_->value()),float(pz_->value())};
            object.node.rotation={float(rx_->value()),float(ry_->value()),float(rz_->value())};
            object.node.scale={float(sx_->value()),float(sy_->value()),float(sz_->value())};
            object.size=float(size_->value());
            object.mass=float(mass_->value());
            object.node.visible=visible_->isChecked();
            object.node.collision=collision_->isChecked();
        });
    });
    statusBar()->showMessage("Qt workspace ready · project scene editor · CPU proxy viewport");
}
void QtEditorWindow::log(QString const& line) {
    if(messages_)messages_->appendPlainText(line);
    statusBar()->showMessage(line,6000);
}
void QtEditorWindow::updateOutliner() {
    QSignalBlocker block(outliner_);
    uiSync_=true;
    outliner_->clear();
    auto filter=outlinerSearch_->text();
    QTreeWidgetItem* active=nullptr;
    for(std::size_t i=0;i<scene_.objects.size();++i) {
        auto const& o=scene_.objects[i];
        QString name=from8(o.name);
        if(!filter.isEmpty() && !name.contains(filter,Qt::CaseInsensitive) &&
           !QString::number(o.node.id).contains(filter))continue;
        auto* item=new QTreeWidgetItem(outliner_,{name,
            o.player?"Player":o.node.asset.empty()?"Primitive":"Asset"});
        item->setData(0,Qt::UserRole,int(i));
        item->setToolTip(0,QString("ID: %1").arg(o.node.id));
        if(int(i)==selection_)active=item;
    }
    if(active)outliner_->setCurrentItem(active);
    uiSync_=false;
}
void QtEditorWindow::updateInspector() {
    bool valid=selection_>=0 && std::size_t(selection_)<scene_.objects.size();
    for(QWidget* w:std::initializer_list<QWidget*>{objectName_,assetPath_,
                     px_,py_,pz_,rx_,ry_,rz_,sx_,sy_,sz_,size_,mass_,visible_,collision_})
        w->setEnabled(valid);
    if(!valid) {
        QSignalBlocker a(objectName_),b(assetPath_);
        objectName_->clear();assetPath_->clear();return;
    }
    auto const& o=scene_.objects[std::size_t(selection_)];
    QSignalBlocker a(objectName_),b(assetPath_);
    objectName_->setText(from8(o.name));
    assetPath_->setText(from8(o.node.asset));
    for(auto [field,value]:{
        std::pair<QDoubleSpinBox*,float>{px_,o.position.x},{py_,o.position.y},
        {pz_,o.position.z},{rx_,o.node.rotation.x},{ry_,o.node.rotation.y},
        {rz_,o.node.rotation.z},{sx_,o.node.scale.x},{sy_,o.node.scale.y},
        {sz_,o.node.scale.z},{size_,o.size},{mass_,o.mass}})field->setValue(value);
    visible_->setChecked(o.node.visible);
    collision_->setChecked(o.node.collision);
}
void QtEditorWindow::updateDependencies() {
    dependencies_->clear();
    if(file_.empty())return;
    QDir root(folderRoot(file_));
    for(auto const& object:scene_.objects) {
        if(object.node.asset.empty())continue;
        auto ref=from8(object.node.asset);
        auto full=root.filePath(ref);
        auto* row=new QTreeWidgetItem(dependencies_,{
            ref,QFileInfo::exists(full)?"Available":"Missing"});
        row->setForeground(1,QBrush(QColor(QFileInfo::exists(full)?
                                           "#8bc98b":"#ed8a8a")));
        row->setToolTip(0,from8(object.name));
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
void QtEditorWindow::chooseObject(int index) {
    if(index<0 || std::size_t(index)>=scene_.objects.size())index=-1;
    selection_=index;
    viewport_->setSelected(index);
    updateOutliner();
    updateInspector();
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
void QtEditorWindow::createActor(QString const& asset,QVector3D point) {
    if(!asset.isEmpty()) {
        if(!safe_virtual_path(utf8(asset))) {
            log("Rejected unsafe asset reference: "+asset);return;
        }
        if(!isMesh(asset)) {
            log("Only mesh resources can be placed. Select an OBJ/YDD/GLTF drawable.");
            return;
        }
        if(file_.empty()) {
            log("Save or open a project scene before placing file-based assets.");
            return;
        }
        if(!QFileInfo::exists(QDir(folderRoot(file_)).filePath(asset))) {
            log("Asset missing in project Content: "+asset);return;
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
    auto material=dir.filePath("Materials/"+name+".material.json");
    if(QFileInfo::exists(material)) {
        QMessageBox::warning(this,"Existing Material","A material with that name exists.");
        return;
    }
    // Explicit authoring descriptor. Native NEF8/YMAT compilation is a separate
    // pipeline, so this JSON file is never passed off as an encoded YMAT.
    QJsonObject attributes{
        {"schema",1},{"name",name},{"shader","standard_lit"},
        {"base_color","#d4dbe5"},{"metallic",0.0},
        {"roughness",0.65},{"base_color_texture",""}};
    QSaveFile output(material);
    if(!output.open(QIODevice::WriteOnly) ||
       output.write(QJsonDocument(attributes).toJson(QJsonDocument::Indented))<0 ||
       !output.commit()) {
        log("Cannot create material descriptor");return;
    }
    browser_->refresh();
    log("Created material authoring descriptor: "+material+
        " (GPU material compiler not yet connected)");
}
bool QtEditorWindow::openScene(QString const& file) {
    try {
        auto path=native(file);
        auto next=load_scene(path);
        validate_scene(next);
        scene_=std::move(next);
        file_=fs::absolute(path);
        history_.clear();dirty_=false;selection_=-1;
        browser_->setContentRoot(folderRoot(file_));
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
        if(changed)browser_->setContentRoot(folderRoot(file_));
        updateAll();
        log("Saved NSCN binary scene: "+qpath(file_));
        return true;
    } catch(std::exception const& e) {
        log("Save failed: "+from8(e.what()));
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
void QtEditorWindow::restoreWorkspace() {
    QSettings settings("KaylasSystems","NorthStarQtEditor");
    auto geometry=settings.value("Workspace/Geometry").toByteArray();
    auto state=settings.value("Workspace/State").toByteArray();
    if(!geometry.isEmpty())restoreGeometry(geometry);
    if(!state.isEmpty())restoreState(state,1);
}
void QtEditorWindow::persistWorkspace() const {
    QSettings settings("KaylasSystems","NorthStarQtEditor");
    settings.setValue("Workspace/Geometry",saveGeometry());
    settings.setValue("Workspace/State",saveState(1));
}
void QtEditorWindow::closeEvent(QCloseEvent* e) {
    if(!confirmDiscard()){e->ignore();return;}
    persistWorkspace();QMainWindow::closeEvent(e);
}
bool QtEditorWindow::runSmoke(QString const& base) {
    try {
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
        return outlinerDock_ && detailsDock_ && contentDock_ && dependencyDock_ &&
               browser_ && viewport_;
    } catch(std::exception const& e){log(from8(e.what()));return false;}
}
}
