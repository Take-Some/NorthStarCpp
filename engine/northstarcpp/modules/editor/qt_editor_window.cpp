#include "qt_editor_window.hpp"
#include "qt_editor_viewport.hpp"
#include "qt_content_browser.hpp"
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
#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QWidget>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <string>

namespace northstarcpp::editor {
namespace fs=std::filesystem;
namespace {
// Drag/drop changes the authoring document, NOT the transient QTreeWidget
// layout. Rebuild the tree from stable NSCN IDs after a validated edit.
class ActorTreeWidget final : public QTreeWidget {
public:
    explicit ActorTreeWidget(QWidget* parent):QTreeWidget(parent) {}
    std::function<void(int,int,QString)> moveActor;
protected:
    void dropEvent(QDropEvent* event) override {
        if(event->source()!=this || !moveActor) {
            event->ignore();
            return;
        }
        auto* source=currentItem();
        auto* target=itemAt(event->position().toPoint());
        if(!source || source->data(0,Qt::UserRole).toInt()<0) {
            event->ignore();
            return;
        }
        int sourceIndex=source->data(0,Qt::UserRole).toInt();
        int targetIndex=target?target->data(0,Qt::UserRole).toInt():-1;
        if(source==target || sourceIndex==targetIndex) {
            event->ignore();
            return;
        }
        QString targetLayer=target && targetIndex<0?target->text(0):QString();
        auto callback=moveActor;
        QTimer::singleShot(0,this,[callback,sourceIndex,targetIndex,targetLayer] {
            callback(sourceIndex,targetIndex,targetLayer);
        });
        event->acceptProposedAction();
    }
};
std::string utf8(QString const& value) {return value.toUtf8().toStdString();}
QString from8(std::string const& value) {return QString::fromUtf8(value.data(),int(value.size()));}
fs::path native(QString const& value) {return fs::u8path(utf8(value));}
QString qpath(fs::path const& value) {return QString::fromUtf8(value.u8string().c_str());}
QVariantList cameraValues(editor_tools::EditorCamera const& camera) {
    auto const& p=camera.pose;
    return {p.position.x,p.position.y,p.position.z,p.yaw,p.pitch,p.roll,
            p.fov_y_degrees};
}
bool applyCameraValues(QtEditorViewport* viewport,QVariant const& data) {
    auto list=data.toList();
    if(list.size()!=7)return false;
    auto pose=viewport->camera().pose;
    bool valid=true;
    std::array<float,7> values{};
    for(std::size_t i=0;i<values.size();++i) {
        bool ok=false;
        auto value=list[int(i)].toFloat(&ok);
        if(!ok || !std::isfinite(value)){valid=false;break;}
        values[i]=value;
    }
    if(!valid || values[6]<5.f || values[6]>150.f)return false;
    pose.position={values[0],values[1],values[2]};
    pose.yaw=values[3];pose.pitch=values[4];pose.roll=values[5];
    pose.fov_y_degrees=values[6];
    viewport->setCameraPose(pose);
    return true;
}
QString sceneCameraKey(fs::path const& file) {
    auto canonical=file.empty()?QByteArray("untitled"):
        QDir::cleanPath(qpath(file)).toUtf8();
    return "Camera/Scenes/"+
        QString::fromLatin1(QCryptographicHash::hash(
            canonical,QCryptographicHash::Sha256).toHex());
}
QString folderRoot(fs::path const& scene) {
    return qpath(scene.parent_path()/"assets");
}
fs::path sharedMaterialRoot() {
    // Resolve the shared library relative to the installed editor or
    // source development build without hardcoding a user's machine path.
    auto parent=fs::u8path(
        QCoreApplication::applicationDirPath().toUtf8().toStdString());
    for(unsigned level=0;level<8;++level) {
        auto const candidate=parent/"sharedAssets";
        std::error_code ec;
        if(fs::is_directory(candidate,ec) &&
           !fs::is_symlink(candidate,ec))
            return candidate;
        if(parent==parent.parent_path())break;
        parent=parent.parent_path();
    }
    return {};
}
bool availableMaterial(fs::path const& root,QString const& reference) {
    if(root.empty() || !material_authoring::is_material(reference))return false;
    try {
        (void)material_authoring::resolve_existing(root,reference);
        return true;
    }catch(...) {return false;}
}
bool validateMaterialComponent(void*,std::uint32_t version,
                               void const* payload,std::size_t size) {
    if(version!=1 || !payload || size==0 || size>1024)return false;
    std::string ref(static_cast<char const*>(payload),size);
    return safe_virtual_path(ref) &&
        material_authoring::is_material(from8(ref));
}
bool isMesh(QString const& path) {
    auto ext=QFileInfo(path).suffix().toLower();
    return QStringList{"ydd","obj","mesh","fbx","gltf","glb"}.contains(ext);
}
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
    register_geometry_primitive_component();
    (void)register_scene_component_type({
        editor_tools::actor_material_component_type,1,1,
        validateMaterialComponent,nullptr});
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
QTreeWidget,QLineEdit,QDoubleSpinBox,QPlainTextEdit,QComboBox {
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
QStatusBar {background:#1d2a3a;color:#e1ebf4;}
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
    auto* add=editMenu->addAction("Add &Primitive",QKeySequence(Qt::CTRL|Qt::Key_Insert),
        [this]{createActor({},{0,0,0});});
    // The Editor and Player both resolve these geometry records via ABI.
    auto* primitives=editMenu->addMenu("Add NorthStar Geometry");
    auto addGeometry=[this,primitives](char const* label,
                                       std::uint32_t kind,std::uint32_t shape,
                                       float major=0.f,float minor=0.f,
                                       std::uint32_t u=0,std::uint32_t v=0) {
        primitives->addAction(QString::fromUtf8(label),
            [this,label,kind,shape,major,minor,u,v] {
                createProceduralActor(QString::fromUtf8(label),
                    {sizeof(NorthStarGeometryRequest),kind,shape,0,
                     major,minor,u,v});
            });
    };
    addGeometry("Cube",NORTHSTAR_GEOMETRY_SOLID,0);
    addGeometry("Cylinder",NORTHSTAR_GEOMETRY_SOLID,3);
    addGeometry("Ellipsoid",NORTHSTAR_GEOMETRY_SOLID,4);
    addGeometry("Ring",NORTHSTAR_GEOMETRY_RING,0,.65f,.24f,24);
    addGeometry("Torus",NORTHSTAR_GEOMETRY_TORUS,0,.65f,.24f,24,12);
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
    playAction_=gameMenu->addAction("Play Game",QKeySequence(Qt::Key_F6),
                                    this,&QtEditorWindow::playGame);
    playAction_->setObjectName("playGameAction");
    stopAction_=gameMenu->addAction("Stop Game",
                                    QKeySequence(Qt::SHIFT|Qt::Key_F6),
                                    this,&QtEditorWindow::stopGame);
    stopAction_->setObjectName("stopGameAction");
    stopAction_->setEnabled(false);
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
    toolbar->addSeparator();
    toolbar->addAction(playAction_);
    toolbar->addAction(stopAction_);
    playerProcess_=new QProcess(this);
    playerProcess_->setProcessChannelMode(QProcess::MergedChannels);
    connect(playerProcess_,&QProcess::started,this,[this] {
        playAction_->setEnabled(false);
        stopAction_->setEnabled(true);
        log("Game player started: "+playerProcess_->program());
    });
    connect(playerProcess_,&QProcess::readyReadStandardOutput,this,[this] {
        auto output=QString::fromUtf8(playerProcess_->readAllStandardOutput());
        if(!output.trimmed().isEmpty()) {
            if(output.size()>2500)output=output.right(2500);
            log("[Player] "+output.trimmed());
        }
    });
    connect(playerProcess_,&QProcess::errorOccurred,this,
            [this](QProcess::ProcessError error) {
        if(error==QProcess::FailedToStart || error==QProcess::Crashed)
            log("Game player error: "+playerProcess_->errorString());
        if(error==QProcess::FailedToStart) {
            playAction_->setEnabled(true);
            stopAction_->setEnabled(false);
        }
    });
    connect(playerProcess_,QOverload<int,QProcess::ExitStatus>::of(
            &QProcess::finished),this,
            [this](int exitCode,QProcess::ExitStatus status) {
        playAction_->setEnabled(true);
        stopAction_->setEnabled(false);
        log(QString("Game player finished (%1, exit %2)")
            .arg(status==QProcess::NormalExit?"normal":"crashed")
            .arg(exitCode));
    });

    auto* modeMove=toolbar->addAction("W Translate");
    auto* modeRotate=toolbar->addAction("E Rotate");
    auto* modeScale=toolbar->addAction("R Scale");
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
    toolbar->addSeparator();
    toolbar->addAction("Focus Selected",QKeySequence(Qt::Key_F),viewport_,
                       &QtEditorViewport::frameSelected);
    toolbar->addAction("Frame All Actors",QKeySequence(Qt::Key_Home),viewport_,
                       &QtEditorViewport::frameAll);
    toolbar->addSeparator();

    auto* viewModes=new QComboBox(toolbar);
    viewModes->setObjectName("viewportDrawMode");
    viewModes->addItems({"Shaded (Proxy)","Wireframe","Shaded + Wireframe",
                         "Engine GPU (D3D11 / Windows)"});
    viewModes->setToolTip(
        "Qt proxy or native engine.render GPU surface; material parity not yet complete");
    toolbar->addWidget(new QLabel(" View: ",toolbar));
    toolbar->addWidget(viewModes);
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
    auto* gridToggle=new QCheckBox("Grid",toolbar);
    gridToggle->setObjectName("viewportGridToggle");
    gridToggle->setChecked(true);
    toolbar->addWidget(gridToggle);
    connect(gridToggle,&QCheckBox::toggled,viewport_,
            &QtEditorViewport::setShowGrid);
    auto* speed=new QDoubleSpinBox(toolbar);
    speed->setObjectName("viewportFlySpeed");
    speed->setToolTip("Fly movement speed; RMB + wheel also adjusts this");
    speed->setRange(.1,2000.);
    speed->setDecimals(1);
    speed->setSuffix(" m/s");
    speed->setValue(viewport_->flySpeed());
    speed->setSingleStep(2.);
    toolbar->addWidget(new QLabel(" Fly speed: ",toolbar));
    toolbar->addWidget(speed);
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
    outliner_=new ActorTreeWidget(left);
    outliner_->setObjectName("actorTree");
    outliner_->setHeaderLabels({"Actor","Type"});
    outliner_->setAlternatingRowColors(true);
    outliner_->setSelectionMode(QAbstractItemView::SingleSelection);
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
    outliner_->header()->setStretchLastSection(false);
    outliner_->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    outliner_->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
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
    auto* apply=new QPushButton("Apply Object Properties",details);
    form->addRow(apply);
    auto* componentGroup=new QGroupBox("ECS Components",details);
    componentGroup->setObjectName("sceneComponentsGroup");
    auto* componentLayout=new QVBoxLayout(componentGroup);
    componentLayout->setContentsMargins(6,8,6,6);
    components_=new QTreeWidget(componentGroup);
    components_->setObjectName("sceneComponentsTree");
    components_->setHeaderLabels({"Component Type","Version","Bytes","Provider"});
    components_->setRootIsDecorated(false);
    components_->setMinimumHeight(120);
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

    for(auto* dock:{outlinerDock_,detailsDock_,contentDock_,dependencyDock_,providerDock_,output})
        viewMenu->addAction(dock->toggleViewAction());
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
        if(!item)return;
        auto index=item->data(0,Qt::UserRole).toInt();
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
            menu.addAction("Expand All",[this]{outliner_->expandAll();});
            menu.addAction("Collapse All",[this]{outliner_->collapseAll();});
        }
        menu.exec(outliner_->viewport()->mapToGlobal(pos));
    });
    connect(outliner_,&QTreeWidget::currentItemChanged,this,
            [this](QTreeWidgetItem* item,QTreeWidgetItem*) {
        if(!uiSync_)chooseObject(item?item->data(0,Qt::UserRole).toInt():-1);
    });
    connect(viewport_,&QtEditorViewport::selectionChanged,this,
            &QtEditorWindow::chooseObject);
    connect(viewport_,&QtEditorViewport::assetDropped,this,
            &QtEditorWindow::handleAssetDrop);
    connect(viewport_,&QtEditorViewport::gizmoDragStarted,this,[this] {
        gizmoBefore_.reset();
        gizmoChanged_=false;
    });
    connect(viewport_,&QtEditorViewport::gizmoDelta,this,
            [this](int mode,int axis,float amount) {
        if(selection_<0 || std::size_t(selection_)>=scene_.objects.size())
            return;
        try {
            // Take one checkpoint for the entire drag, not one full
            // 16K-actor scene snapshot per mouse move.
            if(!gizmoBefore_)gizmoBefore_=scene_;
            editor_tools::apply_gizmo(scene_,std::size_t(selection_),
                static_cast<editor_tools::GizmoMode>(mode),
                static_cast<editor_tools::GizmoAxis>(axis),amount,
                gizmoOptions_);
            gizmoChanged_=true;
            viewport_->update();
            updateInspector();
        } catch(std::exception const& e) {
            if(gizmoBefore_)scene_=std::move(*gizmoBefore_);
            gizmoBefore_.reset();
            gizmoChanged_=false;
            log("Gizmo rejected: "+from8(e.what()));
            updateAll();
        }
    });
    connect(viewport_,&QtEditorViewport::gizmoDragFinished,this,[this] {
        if(gizmoBefore_ && gizmoChanged_) {
            history_.record(std::move(*gizmoBefore_),
                            selection_<0?0:std::size_t(selection_));
            dirty_=true;
        }
        gizmoBefore_.reset();
        gizmoChanged_=false;
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
        auto changed=[&](float currentValue,QDoubleSpinBox const* input) {
            // Only fields actually edited are applied: avoid rounding
            // unrelated precise transforms when renaming an actor.
            return std::abs(double(currentValue)-input->value())>0.000006;
        };
        bool const altered=name!=current.name || reference!=current.node.asset ||
            changed(current.position.x,px_) || changed(current.position.y,py_) ||
            changed(current.position.z,pz_) ||
            changed(current.node.rotation.x,rx_) ||
            changed(current.node.rotation.y,ry_) ||
            changed(current.node.rotation.z,rz_) ||
            changed(current.node.scale.x,sx_) ||
            changed(current.node.scale.y,sy_) ||
            changed(current.node.scale.z,sz_) ||
            changed(current.size,size_) || changed(current.mass,mass_) ||
            visible_->isChecked()!=current.node.visible ||
            collision_->isChecked()!=current.node.collision;
        if(!altered)return;
        editScene([&](Scene& scene) {
            auto& object=scene.objects[std::size_t(selection_)];
            object.name=name;
            object.node.asset=reference;
            auto modify=[&](float& value,QDoubleSpinBox const* field) {
                if(changed(value,field))value=float(field->value());
            };
            modify(object.position.x,px_);
            modify(object.position.y,py_);
            modify(object.position.z,pz_);
            modify(object.node.rotation.x,rx_);
            modify(object.node.rotation.y,ry_);
            modify(object.node.rotation.z,rz_);
            modify(object.node.scale.x,sx_);
            modify(object.node.scale.y,sy_);
            modify(object.node.scale.z,sz_);
            modify(object.size,size_);
            modify(object.mass,mass_);
            object.node.visible=visible_->isChecked();
            object.node.collision=collision_->isChecked();
        });
    };
    connect(apply,&QPushButton::clicked,this,commitInspector);
    connect(objectName_,&QLineEdit::editingFinished,this,commitInspector);
    connect(assetPath_,&QLineEdit::editingFinished,this,commitInspector);
    for(auto* field:{px_,py_,pz_,rx_,ry_,rz_,sx_,sy_,sz_,size_,mass_})
        connect(field,&QDoubleSpinBox::editingFinished,this,commitInspector);
    connect(visible_,&QCheckBox::clicked,this,commitInspector);
    connect(collision_,&QCheckBox::clicked,this,commitInspector);
    statusBar()->showMessage(
        "Editor ready · RMB+WASD fly · Q/E vertical · View modes · F6 Play Game");
}
void QtEditorWindow::log(QString const& line) {
    if(messages_)messages_->appendPlainText(line);
    statusBar()->showMessage(line,6000);
}
void QtEditorWindow::updateOutliner() {
    QSignalBlocker block(outliner_);
    uiSync_=true;
    // Preserve the user's collapsed actor branches across property changes.
    // The model is rebuilt only after actual scene edits/search changes.
    std::unordered_set<std::uint64_t> collapsed;
    if(outlinerSearch_->text().trimmed().isEmpty()) {
        QTreeWidgetItemIterator iter(outliner_);
        while(*iter) {
            auto* node=*iter;
            if(node->data(0,Qt::UserRole).toInt()>=0 && !node->isExpanded())
                collapsed.insert(node->data(0,Qt::UserRole+1).toULongLong());
            ++iter;
        }
    }
    outliner_->clear();
    QString const filter=outlinerSearch_->text().trimmed();
    std::unordered_map<std::uint64_t,std::size_t> byId;
    byId.reserve(scene_.objects.size());
    for(std::size_t i=0;i<scene_.objects.size();++i)
        byId.emplace(scene_.objects[i].node.id,i);

    std::vector<bool> visible(scene_.objects.size(),filter.isEmpty());
    if(!filter.isEmpty()) {
        for(std::size_t i=0;i<scene_.objects.size();++i) {
            auto const& object=scene_.objects[i];
            if(!from8(object.name).contains(filter,Qt::CaseInsensitive) &&
               !from8(object.node.layer).contains(filter,Qt::CaseInsensitive) &&
               !QString::number(object.node.id).contains(filter))continue;
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
        bool const procedural=std::any_of(scene_.components.begin(),
            scene_.components.end(),[&](SceneComponentBlob const& component) {
                return component.entity_id==object.node.id &&
                       component.type==geometry_primitive_component;
            });
        auto* item=new QTreeWidgetItem({
            from8(object.name),
            object.player?"Player":procedural?"Geometry":
            object.node.asset.empty()?"Primitive":"Asset"});
        item->setData(0,Qt::UserRole,int(i));
        item->setData(0,Qt::UserRole+1,qulonglong(object.node.id));
        item->setToolTip(0,QString("Entity ID: %1 | Layer: %2")
                              .arg(object.node.id).arg(from8(object.node.layer)));
        if(!object.node.visible)
            item->setForeground(0,QColor("#77889a"));
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
                auto* folder=new QTreeWidgetItem(outliner_,{
                    from8(layer),"Layer"});
                folder->setData(0,Qt::UserRole,-1);
                folder->setExpanded(true);
                it=layers.emplace(layer,folder).first;
            }
            it->second->addChild(entries[i]);
        }
    }
    if(!filter.isEmpty())
        outliner_->expandAll();
    else {
        QTreeWidgetItemIterator iter(outliner_);
        while(*iter) {
            auto* item=*iter;
            if(item->data(0,Qt::UserRole).toInt()>=0)
                item->setExpanded(collapsed.find(item->data(0,Qt::UserRole+1).toULongLong())==
                    collapsed.end());
            ++iter;
        }
    }
    if(active) {
        outliner_->setCurrentItem(active);
        auto* parent=active->parent();
        while(parent) {parent->setExpanded(true);parent=parent->parent();}
    }
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
    updateComponentInspector();
    bool valid=selection_>=0 && std::size_t(selection_)<scene_.objects.size();
    for(QWidget* w:std::initializer_list<QWidget*>{objectName_,assetPath_,
                     px_,py_,pz_,rx_,ry_,rz_,sx_,sy_,sz_,size_,mass_,
                     visible_,collision_,materialPicker_})
        w->setEnabled(valid);
    refreshMaterialChoices();
    QSignalBlocker materialBlock(materialPicker_);
    if(!valid) {
        QSignalBlocker a(objectName_),b(assetPath_);
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
    auto* payload=new QPlainTextEdit(&dialog);
    payload->setObjectName("componentHexPayload");
    payload->setPlainText(QString::fromLatin1(
        QByteArray(existing.payload.data(),int(existing.payload.size())).toHex(' ')));
    payload->setPlaceholderText("Hexadecimal bytes (empty is permitted)");
    payload->setMinimumHeight(230);
    fields->addRow("Binary Payload (hex)",payload);
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
            auto const found=QFileInfo::exists(root.filePath(ref));
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
void QtEditorWindow::chooseObject(int index) {
    if(index<0 || std::size_t(index)>=scene_.objects.size())index=-1;
    selection_=index;
    viewport_->setSelected(index);
    updateOutliner();
    updateInspector();
}
void QtEditorWindow::renameSelected() {
    if(selection_<0 || std::size_t(selection_)>=scene_.objects.size())return;
    bool ok=false;
    auto name=QInputDialog::getText(this,"Rename Actor","Actor name:",
        QLineEdit::Normal,from8(scene_.objects[std::size_t(selection_)].name),
        &ok).trimmed();
    if(ok && !name.isEmpty()) {
        editScene([this,name](Scene& scene) {
            scene.objects[std::size_t(selection_)].name=utf8(name);
        });
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
    static auto const defaultImage=
        material_authoring::neutral_sphere_preview(96);
    auto const placeholder=QIcon(QPixmap::fromImage(defaultImage));
    auto addOption=[this,&placeholder](QString const& name,QString const& source) {
        auto const loaded=materialSpheres_.value(name);
        materialPicker_->addItem(loaded.isNull()?placeholder:
            QIcon(QPixmap::fromImage(loaded)),source+" · "+name,name);
        materialPicker_->setItemData(materialPicker_->count()-1,
            source+" material: "+name+"\nStudio sphere shows texture and PBR settings",
            Qt::ToolTipRole);
    };
    for(auto const& name:projectMaterials)addOption(name,"[Project]");
    for(auto const& name:sharedMaterials) {
        if(cachedMaterials_.contains(name))continue; // VFS project override
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
        for(auto const& result:results) {
            auto const& ref=result.first;
            auto const& preview=result.second;
            // Missing/unsupported material previews use a neutral studio
            // sphere rather than restarting a failed decode indefinitely.
            auto const image=preview.isNull()?
                material_authoring::neutral_sphere_preview(96):preview;
            materialSpheres_.insert(ref,image);
            auto const index=materialPicker_->findData(ref);
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
                viewport_->setAssetSources(file_.parent_path());
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
            viewport_->setAssetSources(file_.parent_path());
            refreshMaterialChoices(true);
            updateInspector();
            viewport_->update();
        }catch(std::exception const& e) {
            QMessageBox::warning(this,"Material Editor",from8(e.what()));
        }
        return;
    }
    if(!reference.endsWith(".ymat",Qt::CaseInsensitive) &&
       !reference.endsWith(".material.json",Qt::CaseInsensitive)) {
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
        dialog.resize(1150,790);
        auto* layout=new QVBoxLayout(&dialog);
        auto* tabs=new QTabWidget(&dialog);
        tabs->setObjectName("materialEditorTabs");
        auto* properties=new QWidget(tabs);
        auto* surfaceLayout=new QVBoxLayout(properties);
        auto* graphEditor=new MaterialGraphEditor(
            original.value("graph").toObject(),tabs,root);
        auto* gpuPreview=new MaterialGpuPreview(root,tabs);
        tabs->addTab(gpuPreview,"Preview");
        tabs->addTab(properties,"Surface");
        tabs->addTab(graphEditor,"Material Graph");
        layout->addWidget(tabs,1);
        // Material Editor has its own workspace; do not reset preferred
        // panel widths and active tab every time an asset is opened.
        QSettings workspace("KaylasSystems","NorthStarQtEditor");
        dialog.restoreGeometry(workspace.value(
            "MaterialEditor/WindowGeometry").toByteArray());
        tabs->setCurrentIndex(std::clamp(
            workspace.value("MaterialEditor/ActiveTab",0).toInt(),0,2));
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
            viewport_->setAssetSources(file_.parent_path());
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
    refreshMaterialChoices(true);
    updateInspector();
    log("Created project material: "+material+
        " (native renderer material compilation remains a separate step)");
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
        browser_->setContentRoot(folderRoot(file_));
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
            browser_->setContentRoot(folderRoot(file_));
            viewport_->setAssetSources(file_.parent_path());
        }
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
QString QtEditorWindow::playerExecutable() const {
    QString const env=qEnvironmentVariable("NORTHSTARCPP_PLAYER_EXECUTABLE");
    if(!env.isEmpty() && QFileInfo(env).isFile())
        return QFileInfo(env).canonicalFilePath();
    if(!configuredPlayerPath_.isEmpty() &&
       QFileInfo(configuredPlayerPath_).isFile())
        return QFileInfo(configuredPlayerPath_).canonicalFilePath();
    QDir const binary(QCoreApplication::applicationDirPath());
    for(QString const& candidate:QStringList{
        binary.filePath("northstarcpp_player.exe"),
        binary.filePath("../northstarcpp_player.exe"),
        binary.filePath("../bin/northstarcpp_player.exe"),
        binary.filePath("../../bin/northstarcpp_player.exe")}) {
        QFileInfo info(candidate);
        if(info.isFile())return info.canonicalFilePath();
    }
    return {};
}
void QtEditorWindow::choosePlayerExecutable() {
    auto file=QFileDialog::getOpenFileName(this,"Locate NorthStar Game Player",
        configuredPlayerPath_.isEmpty()?QCoreApplication::applicationDirPath():
                                        configuredPlayerPath_,
        "NorthStar Player (northstarcpp_player.exe);;Executable (*.exe)");
    if(file.isEmpty())return;
    QFileInfo info(file);
    if(!info.isFile() ||
       info.fileName().compare("northstarcpp_player.exe",Qt::CaseInsensitive)!=0) {
        QMessageBox::warning(this,"Invalid Player",
            "Select the actual NorthStarCpp Player (northstarcpp_player.exe).");
        return;
    }
    configuredPlayerPath_=info.canonicalFilePath();
    QSettings("KaylasSystems","NorthStarQtEditor").setValue(
        "Game/PlayerExecutable",configuredPlayerPath_);
    log("Configured Game Player: "+configuredPlayerPath_);
}
void QtEditorWindow::playGame() {
    if(!playerProcess_ || playerProcess_->state()!=QProcess::NotRunning) {
        log("Game player is already running");return;
    }
    if(file_.empty() || dirty_) {
        if(!saveScene()) {
            log("Game launch cancelled: save current scene first");return;
        }
    }
    if(file_.empty() || !fs::is_regular_file(file_)) {
        log("Game launch requires a saved project scene");return;
    }
    auto const executable=playerExecutable();
    if(executable.isEmpty()) {
        log("Game Player not found. Build northstarcpp_player or use "
            "Game > Set Player Executable...");
        QMessageBox::warning(this,"Game Player Not Found",
            "The NorthStarCpp Player executable could not be found.\n"
            "Build the northstarcpp_player target or select its path "
            "under Game > Set Player Executable...");
        return;
    }
    QStringList args{"--scene",qpath(fs::absolute(file_))};
    // Project <scene>/assets is mounted automatically by Player, at normal
    // project priority. NEVER pass it as --shared-assets: that option denotes
    // a separate low-priority engine-wide asset root. Passing the project
    // folder twice caused "Asset source already mounted" in engine.assets.
    QString sharedRoot;
    auto tryShared=[&](QDir parent) {
        for(int depth=0;depth<8;++depth) {
            QFileInfo candidate(parent.filePath("sharedAssets"));
            if(candidate.isDir()) {
                sharedRoot=candidate.canonicalFilePath();
                return;
            }
            if(!parent.cdUp())break;
        }
    };
    tryShared(QDir(qpath(fs::absolute(file_).parent_path())));
    if(sharedRoot.isEmpty())
        tryShared(QDir(QFileInfo(executable).absolutePath()));
    if(!sharedRoot.isEmpty()) {
        QString const projectAssets=QFileInfo(folderRoot(file_)).canonicalFilePath();
        if(projectAssets.isEmpty() ||
           sharedRoot.compare(projectAssets,Qt::CaseInsensitive)!=0)
            args<<"--shared-assets"<<sharedRoot;
    }
    if(exploreModeAction_ && exploreModeAction_->isChecked())
        args<<"--explore";
    else if(fpsModeAction_ && fpsModeAction_->isChecked())
        args<<"--fps";
    playerProcess_->setProgram(executable);
    playerProcess_->setArguments(args);
    playerProcess_->setWorkingDirectory(QFileInfo(executable).absolutePath());
    playAction_->setEnabled(false);
    stopAction_->setEnabled(true);
    log("Starting game: "+executable+" "+args.join(" "));
    playerProcess_->start(QIODevice::ReadOnly);
}
void QtEditorWindow::stopGame() {
    if(!playerProcess_ || playerProcess_->state()==QProcess::NotRunning)return;
    playerProcess_->terminate();
    QPointer<QProcess> process(playerProcess_);
    QTimer::singleShot(2500,this,[process] {
        if(process && process->state()!=QProcess::NotRunning)
            process->kill(); // never terminate unrelated user processes
    });
}
bool QtEditorWindow::runPlayerCheckSmoke(QString const& root) {
    QDir dir(root);
    if(!dir.mkpath("."))return false;
    auto scene=dir.filePath("launch-smoke.nscn");
    if(!saveScene(scene))return false;
    QString const player=playerExecutable();
    if(player.isEmpty()) {
        log("Player smoke cannot run without northstarcpp_player.exe");
        return false;
    }
    QProcess check;
    check.setProgram(player);
    check.setArguments({"--check-scene","--scene",scene});
    check.setWorkingDirectory(QFileInfo(player).absolutePath());
    check.start(QIODevice::ReadOnly);
    if(!check.waitForStarted(8000)) {
        log("Player startup failed: "+check.errorString());return false;
    }
    if(!check.waitForFinished(30000)) {
        check.kill();check.waitForFinished(2000);
        log("Player scene validation timed out");return false;
    }
    if(check.exitStatus()!=QProcess::NormalExit || check.exitCode()!=0) {
        log("Player validation failure: "+
            QString::fromUtf8(check.readAll()).right(500));
        return false;
    }
    return true;
}
void QtEditorWindow::restoreWorkspace() {
    QSettings settings("KaylasSystems","NorthStarQtEditor");
    auto geometry=settings.value("Workspace/Geometry").toByteArray();
    auto state=settings.value("Workspace/State").toByteArray();
    if(!geometry.isEmpty())restoreGeometry(geometry);
    if(!state.isEmpty())restoreState(state,1);
    configuredPlayerPath_=settings.value("Game/PlayerExecutable").toString();
    if(exploreModeAction_)
        exploreModeAction_->setChecked(settings.value("Game/Explore",false).toBool());
    if(fpsModeAction_ && !exploreModeAction_->isChecked())
        fpsModeAction_->setChecked(settings.value("Game/FPS",false).toBool());
    if(viewport_) {
        viewport_->setFlySpeed(settings.value("Viewport/FlySpeed",12.).toFloat());
        viewport_->setShowGrid(settings.value("Viewport/Grid",true).toBool());
        viewport_->setDrawMode(static_cast<QtEditorViewport::DrawMode>(
            std::clamp(settings.value("Viewport/DrawMode",0).toInt(),0,3)));
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
    if(!confirmDiscard()){e->ignore();return;}
    if(playerProcess_ && playerProcess_->state()!=QProcess::NotRunning)
        stopGame();
    persistWorkspace();QMainWindow::closeEvent(e);
}
bool QtEditorWindow::runSmoke(QString const& base) {
    try {
        refreshProviderInspector();
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
        outlinerSearch_->clear();
        // Gizmo drag must create a single Undo step.
        auto const original=scene_.objects[std::size_t(actor)].position.x;
        chooseObject(actor);
        viewport_->gizmoDragStarted();
        viewport_->gizmoDelta(0,0,1.2f);
        viewport_->gizmoDelta(0,0,1.2f);
        viewport_->gizmoDragFinished();
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
        if(!saveScene() || !openScene(sceneFile) ||
           scene_.objects[std::size_t(actor)].node.parent!=parentId) {
            log("smoke: NEF8 actor parent roundtrip failed");return false;
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
        std::string const opaqueBytes{char(0),char(0xff),char(0xa5),char(0x0a)};
        editScene([entityId,&opaqueBytes](Scene& scene) {
            scene.components.push_back(SceneComponentBlob{
                entityId,"com.kaylasystems.editor.smoke",17,opaqueBytes});
        },actor);
        if(scene_.components.empty() || !components_ ||
           components_->topLevelItemCount()!=1 ||
           components_->topLevelItem(0)->text(3)!="Opaque / Unavailable") {
            log("smoke: plugin component inspector lost opaque record");return false;
        }
        undo();
        if(!scene_.components.empty()) {
            log("smoke: component Undo failed");return false;
        }
        redo();
        if(scene_.components.size()!=1 ||
           scene_.components[0].payload!=opaqueBytes) {
            log("smoke: component Redo lost binary payload");return false;
        }
        if(!saveScene() || !openScene(sceneFile) ||
           scene_.components.size()!=1 ||
           scene_.components[0].payload!=opaqueBytes ||
           scene_.components[0].version!=17 ||
           scene_.components[0].entity_id!=entityId) {
            log("smoke: NEF8 opaque ECS component roundtrip failed");return false;
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
        if(!dialogPopulated || scene_.components.size()!=1 ||
           scene_.components[0].payload!=changedBytes ||
           scene_.components[0].version!=18) {
            log("smoke: raw ECS component dialog failed");return false;
        }
        undo();
        if(scene_.components.size()!=1 ||
           scene_.components[0].payload!=opaqueBytes ||
           scene_.components[0].version!=17) {
            log("smoke: component dialog Undo failed");return false;
        }
        redo();
        if(scene_.components.size()!=1 ||
           scene_.components[0].payload!=changedBytes ||
           scene_.components[0].version!=18) {
            log("smoke: component dialog Redo failed");return false;
        }
        if(!saveScene() || !openScene(sceneFile) ||
           scene_.components.size()!=1 ||
           scene_.components[0].payload!=changedBytes ||
           scene_.components[0].version!=18) {
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
        if(descriptor.value("base_color_texture").toString()!="Textures/editor-smoke.png" ||
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
            auto const index=materialPicker_->findData(
                QString("characters/abby/materials/abby.ymat"));
            if(index<1) {
                log("smoke: shared YMAT missing from material picker");return false;
            }
            materialPicker_->setCurrentIndex(index);
            if(editor_tools::actor_material(scene_,std::size_t(actor))!=
               "characters/abby/materials/abby.ymat") {
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
        return outlinerDock_ && detailsDock_ && contentDock_ && dependencyDock_ &&
               browser_ && viewport_;
    } catch(std::exception const& e){log(from8(e.what()));return false;}
}

bool QtEditorWindow::runGpuSmoke(QString const& root) {
#ifdef _WIN32
    if(!runSmoke(root))return false;
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
    if(diagnosticsEnabled) {
        diagnostic.write(("Draw: "+QString::number(rendered)+"\nStatus: "+
             statusBar()->currentMessage()+"\n").toUtf8());
        diagnostic.flush();
    }
    viewport_->setDrawMode(QtEditorViewport::DrawMode::Shaded);
    if(!rendered) {
        qWarning() << "GPU QA: frame draw failed:"
                   << statusBar()->currentMessage();
        log("smoke: native GPU draw failed");
    }
    return rendered;
#else
    Q_UNUSED(root);
    log("GPU HWND provider is Windows-only; a portable GPU surface is pending.");
    return false;
#endif
}
}
