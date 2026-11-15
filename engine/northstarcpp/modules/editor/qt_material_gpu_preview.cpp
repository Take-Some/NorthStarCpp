#include "qt_material_gpu_preview.hpp"
#include "qt_editor_viewport.hpp"
#include "qt_material_authoring.hpp"
#include "../render/shared/material_variants.hpp"
#include <northstarcpp/material.hpp>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonArray>
#include "editor_tools.hpp"
#include <northstarcpp/geometry_scene.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSlider>
#include <cmath>
#include <sstream>
#include <locale>
#include <fstream>
#include <iterator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSet>
#include <QHBoxLayout>
#include <QLabel>
#include <QSaveFile>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace northstarcpp::editor {
namespace {
constexpr auto previewReference="Materials/Preview.ymat";
std::filesystem::path diskPath(QString const& path) {
    return std::filesystem::u8path(path.toUtf8().toStdString());
}
QString qpath(std::filesystem::path const& path) {
    auto s=path.generic_u8string();
    return QString::fromUtf8(s.data(),int(s.size()));
}
northstarcpp::MaterialFunctionResolver functionResolver(
    std::filesystem::path const& root) {
    return [root](std::string const& reference) {
        return material_authoring::material_function_source(root,reference);
    };
}
} // namespace

MaterialGpuPreview::MaterialGpuPreview(
    std::filesystem::path contentRoot,QWidget* parent)
    :QWidget(parent),contentRoot_(std::move(contentRoot)),
     scratch_(QStringLiteral("northstar-material-preview-XXXXXX")) {
    setObjectName("northstarMaterialGpuPreview");
    setMinimumWidth(330);
    auto* layout=new QVBoxLayout(this);
    auto* title=new QLabel("Material Preview  |  engine.render",this);
    title->setStyleSheet("font-weight:600;color:#d5e2ec;");
    layout->addWidget(title);
    auto* control=new QHBoxLayout;
    mesh_=new QComboBox(this);
    mesh_->setObjectName("materialPreviewMesh");
    mesh_->addItems({"Sphere","Cube","Torus"});
    grid_=new QCheckBox("GPU Grid",this);
    grid_->setChecked(false);
    grid_->setObjectName("materialPreviewGrid");
    grid_->setToolTip("Display a non-colliding 3D reference grid in the GPU scene.");
    control->addWidget(mesh_,1);
    control->addWidget(grid_);
    layout->addLayout(control);
    // The existing engine.render.light Scene V3 bridge supplies this key light
    // to providers which support deferred dynamic lighting (no ABI change).
    auto* lighting=new QHBoxLayout;
    auto* power=new QDoubleSpinBox(this);
    power->setObjectName("materialPreviewLightIntensity");
    power->setRange(0.,20.);
    power->setSingleStep(.25);
    power->setValue(3.);
    power->setToolTip("Key light intensity; rendered via engine.render.light");
    auto* azimuth=new QSlider(Qt::Horizontal,this);
    azimuth->setObjectName("materialPreviewLightAzimuth");
    azimuth->setRange(-180,180);
    azimuth->setValue(30);
    lighting->addWidget(new QLabel("Key Light",this));
    lighting->addWidget(power);
    lighting->addWidget(new QLabel("Angle",this));
    lighting->addWidget(azimuth,1);
    layout->addLayout(lighting);
    auto updateKeyLight=[this,power,azimuth] {
        scene_.components.erase(
            std::remove_if(scene_.components.begin(),scene_.components.end(),
                [](auto const& c){return c.type=="engine.render.light";}),
            scene_.components.end());
        auto const angle=double(azimuth->value())*3.141592653589793/180.;
        northstarcpp::SceneComponentBlob light{};
        light.type="engine.render.light";
        light.version=1;
        light.entity_id=0; // absolute world-space studio fixture
        std::ostringstream value;
        value.imbue(std::locale::classic());
        value.precision(8);
        value<<(3.*std::sin(angle))<<" 3 "<<(3.*std::cos(angle))
             <<" 9 1 0.95 0.86 "<<power->value()
             <<" 0 -1 0 -1";
        light.payload=value.str();
        scene_.components.push_back(std::move(light));
        if(viewport_) {
            viewport_->setScene(&scene_);
            if(started_&&viewport_->gpuActive())viewport_->renderGpuNow();
        }
    };
    connect(power,QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,[updateKeyLight](double){updateKeyLight();});
    connect(azimuth,&QSlider::valueChanged,this,
            [updateKeyLight](int){updateKeyLight();});
    updateKeyLight();
    viewport_=new QtEditorViewport(this);
    viewport_->setObjectName("materialGpuPreviewViewport");
    viewport_->setMinimumHeight(300);
    layout->addWidget(viewport_,1);
    status_=new QLabel("Preparing native GPU surface...",this);
    status_->setObjectName("materialGpuPreviewStatus");
    status_->setWordWrap(true);
    status_->setStyleSheet("color:#a6bbca;");
    layout->addWidget(status_);
    throttle_=new QTimer(this);
    throttle_->setSingleShot(true);
    throttle_->setInterval(250);
    connect(throttle_,&QTimer::timeout,this,[this]{updatePreview();});
    functionWatcher_=new QFileSystemWatcher(this);
    connect(functionWatcher_,&QFileSystemWatcher::fileChanged,this,
            [this](QString const&) {
        ++revision_;
        refreshFunctionWatchers(); // QSaveFile can replace the watched inode.
        throttle_->start();        // Function edits must invalidate GPU preview.
    });
    connect(mesh_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[this](int index){setMesh(index);});
    connect(grid_,&QCheckBox::toggled,this,[this](bool enabled){setGrid(enabled);});
    connect(viewport_,&QtEditorViewport::gpuUnavailable,this,
            [this](QString const& error){
                status_->setText("GPU preview unavailable: "+error+
                    ". Scene uses the safe Qt proxy; no material was saved.");
                status_->setStyleSheet("color:#e8a1a1;");
            });
    scene_.version=3;
    scene_.name="Material Preview";
    scene_.layers.push_back({"Default",true});
    auto actor=northstarcpp::SceneObject{};
    actor.name="Preview Object";
    actor.node.id=1;
    actor.node.layer="Default";
    actor.node.collision=false;
    actor.size=2.0f;
    scene_.objects.push_back(std::move(actor));
    northstarcpp::register_geometry_primitive_component();
    northstarcpp::editor_tools::assign_actor_material(
        scene_,0,previewReference);
    setMesh(0);
    viewport_->setScene(&scene_);
    viewport_->setSelected(-1);
    viewport_->setShowGrid(false);
    northstarcpp::CameraPose camera=viewport_->camera().pose;
    camera.position={2.5f,1.7f,-3.6f};
    camera.yaw=-0.61f;camera.pitch=-0.3f;
    viewport_->setCameraPose(camera);
    viewport_->setOrbitMode(true,{0.f,1.f,0.f},4.5f);
    if(scratch_.isValid())
        viewport_->setAssetSources(diskPath(scratch_.path()));
}
void MaterialGpuPreview::setMesh(int index) {
    if(scene_.objects.empty())return;
    scene_.components.erase(
        std::remove_if(scene_.components.begin(),scene_.components.end(),
                       [](auto const& component){
            return component.type==northstarcpp::geometry_primitive_component;
        }),scene_.components.end());
    NorthStarGeometryRequest request{};
    request.size=sizeof(request);
    if(index==2) {
        request.kind=NORTHSTAR_GEOMETRY_TORUS;
        request.a=.78f;
        request.b=.29f;
        request.u=40;
        request.v=20;
    }else {
        request.kind=NORTHSTAR_GEOMETRY_SOLID;
        request.shape=index==1?0u:4u;
        request.a=1.f;request.b=1.f;
        // SOLID shapes use fixed provider tessellation: u/v must be zero.
        request.u=0;request.v=0;
    }
    scene_.components.push_back(
        northstarcpp::make_geometry_primitive_component(1,request));
    if(viewport_) {
        viewport_->setScene(&scene_);
        if(started_ && viewport_->gpuActive())viewport_->renderGpuNow();
    }
}
void MaterialGpuPreview::setGrid(bool enabled) {
    if(scene_.objects.empty())return;
    scene_.objects.resize(1);
    if(enabled) {
        constexpr float extent=4.f;
        for(int i=-4;i<=4;++i) {
            auto addLine=[&](bool alongX) {
                northstarcpp::SceneObject line{};
                line.name="Preview Grid";
                line.node.id=std::uint64_t(100+scene_.objects.size());
                line.node.layer="Default";
                line.node.collision=false;
                line.position={alongX?0.f:float(i),-.06f,
                               alongX?float(i):0.f};
                line.size=1.f;
                line.node.scale=alongX?
                    northstarcpp::Vector3{extent*2.f,.015f,.015f}:
                    northstarcpp::Vector3{.015f,.015f,extent*2.f};
                line.color=i==0?0x618ca4u:0x344b5fu;
                scene_.objects.push_back(std::move(line));
            };
            addLine(true);
            addLine(false);
        }
    }
    viewport_->setScene(&scene_);
    if(started_ && viewport_->gpuActive())viewport_->renderGpuNow();
}
bool MaterialGpuPreview::gpuActive() const {
    return viewport_ && viewport_->gpuActive();
}
QString MaterialGpuPreview::diagnostic() const {
    return status_?status_->text():QString{};
}
void MaterialGpuPreview::start() {
    if(started_)return;
    if(!scratch_.isValid()) {
        status_->setText("Cannot create isolated preview assets directory.");
        return;
    }
    // The very first GPU frame must never resolve a missing preview material.
    // Validate/stage before creating the engine.render swapchain.
    QString diagnostic;
    if(pending_.isEmpty() || !stage(pending_,&diagnostic)) {
        status_->setText("Preview material not ready: "+diagnostic);
        return;
    }
    started_=true;
    viewport_->setDrawMode(QtEditorViewport::DrawMode::EngineGpu);
    if(!viewport_->gpuActive()) {
        status_->setText("GPU preview unavailable; renderer or graphics driver not ready.");
        return;
    }
    if(viewport_->renderGpuNow()) {
        status_->setText(diagnostic);
        status_->setStyleSheet("color:#a6d3bc;");
    }
}
void MaterialGpuPreview::setMaterial(QJsonObject descriptor) {
    pending_=std::move(descriptor);
    ++revision_; // In-flight compile results from older edits are discarded.
    refreshFunctionWatchers();
    throttle_->start();
}
void MaterialGpuPreview::refreshFunctionWatchers() {
    if(!functionWatcher_)return;
    auto previous=functionWatcher_->files();
    if(!previous.isEmpty())functionWatcher_->removePaths(previous);
    QSet<QString> visited;
    std::function<void(QJsonObject const&,unsigned)> traverse;
    traverse=[&](QJsonObject const& graph,unsigned depth) {
        if(depth>4||visited.size()>16)return;
        for(auto const& raw:graph.value("nodes").toArray()) {
            auto node=raw.toObject();
            if(node.value("op").toString()!="function")continue;
            auto ref=node.value("path").toString();
            if(visited.contains(ref)||ref.isEmpty())continue;
            visited.insert(ref);
            try {
                auto path=material_authoring::resolve_existing(contentRoot_,ref);
                auto full=qpath(path);
                functionWatcher_->addPath(full);
                auto source=QByteArray::fromStdString(
                    material_authoring::material_function_source(
                        contentRoot_,ref.toUtf8().toStdString()));
                auto descriptor=QJsonDocument::fromJson(source);
                if(descriptor.isObject())traverse(descriptor.object(),depth+1);
            }catch(std::exception const&) {
                // The async graph compiler provides the actual error.
            }
        }
    };
    traverse(pending_.value("graph").toObject(),0);
}
bool MaterialGpuPreview::stage(QJsonObject const& descriptor,QString* reason) {
    if(!scratch_.isValid()) {
        if(reason)*reason="Isolated preview directory is not available.";
        return false;
    }
    namespace ma=material_authoring;
    try {
        QJsonObject staged=descriptor;
        // Temporary assets never touch or overwrite the edited project file.
        // Only validated, bounded existing project textures may be mirrored.
        constexpr std::uintmax_t maxTextureBytes=64ull*1024*1024;
        QStringList absent;
        auto assets=diskPath(scratch_.path())/"assets";
        std::error_code ec;
        std::filesystem::create_directories(assets/"Materials",ec);
        if(ec)throw std::runtime_error("Cannot create preview material assets");
        for(auto const* key:{"base_color_texture","normal_texture",
                             "roughness_texture","metallic_texture",
                             "emissive_texture"}) {
            QString ref=staged.value(key).toString();
            if(ref.isEmpty())continue;
            try {
                auto file=ma::resolve_existing(contentRoot_,ref);
                if(!ma::is_texture(ref))
                    throw std::invalid_argument("Unsupported preview texture");
                auto const size=std::filesystem::file_size(file);
                if(size>maxTextureBytes)
                    throw std::invalid_argument("Texture exceeds preview budget");
                auto target=assets/diskPath(ref);
                std::filesystem::create_directories(target.parent_path(),ec);
                if(ec)throw std::runtime_error("Cannot stage preview texture");
                if(!QFile::exists(qpath(target)) ||
                   QFileInfo(qpath(target)).size()!=qint64(size)) {
                    QFile::remove(qpath(target));
                    if(!QFile::copy(qpath(file),qpath(target)))
                        throw std::runtime_error("Cannot mirror preview texture");
                }
            }catch(std::exception const&) {
                // Missing sources are signaled; they do not crash authoring.
                absent.append(QString::fromLatin1(key)+": "+ref);
                staged.insert(key,"");
            }
        }
        // Expand Material Functions from the project VFS before staging the
        // isolated GPU file: the scratch root has no access to authoring files.
        if(staged.value("graph").isObject()) {
            auto const input=QJsonDocument(staged.value("graph").toObject())
                .toJson(QJsonDocument::Compact);
            auto lowered=northstarcpp::expand_material_graph_functions(
                input.toStdString(),functionResolver(contentRoot_));
            auto document=QJsonDocument::fromJson(
                QByteArray::fromStdString(lowered));
            if(!document.isObject())
                throw std::runtime_error("Invalid expanded material function graph");
            staged["graph"]=document.object();
        }
        ma::save_descriptor(assets,previewReference,staged);
        if(reason) {
            *reason=absent.isEmpty()?
                "Live GPU PBR preview (engine.render). Drag: orbit; wheel: zoom.":
                "Preview missing "+QString::number(absent.size())+
                " texture(s): "+absent.join("; ");
        }
        return true;
    }catch(std::exception const& e) {
        if(reason)*reason=QString::fromUtf8(e.what());
        return false;
    }
}
bool MaterialGpuPreview::stageForTest(QJsonObject descriptor,QString* reason) {
    return stage(descriptor,reason);
}
void MaterialGpuPreview::updatePreview() {
    if(pending_.isEmpty())return;
    auto descriptor=pending_;
    if(!descriptor.contains("graph")) {
        applyPreflight(descriptor,{});
        return;
    }
    QByteArray const graph=QJsonDocument(descriptor.value("graph").toObject())
        .toJson(QJsonDocument::Compact);
    auto const graphNodes=descriptor.value("graph").toObject()
        .value("nodes").toArray();
    bool const hasFunctions=std::any_of(
        graphNodes.begin(),graphNodes.end(),
        [](QJsonValue const& node) {
            return node.toObject().value("op").toString()=="function";
        });
    // External function files may change without the parent graph changing.
    // Do not reuse graph-only preflight cache entries for such dependencies.
    if(!hasFunctions && preflightCache_.contains(graph)) {
        applyPreflight(descriptor,preflightCache_.value(graph));
        return;
    }
    quint64 const revision=revision_;
    auto* watcher=new QFutureWatcher<QString>(this);
    connect(watcher,&QFutureWatcher<QString>::finished,this,
            [this,watcher,descriptor,graph,revision,hasFunctions] {
        QString const diagnostic=watcher->result();
        watcher->deleteLater();
        if(revision!=revision_)return;
        if(!hasFunctions) {
            if(preflightCache_.size()>=64)preflightCache_.clear();
            preflightCache_.insert(graph,diagnostic);
        }
        applyPreflight(descriptor,diagnostic);
    });
    auto const root=contentRoot_;
    watcher->setFuture(QtConcurrent::run([graph,root]() -> QString {
        try {
            std::string hlsl=northstarcpp::compile_material_graph_with_functions(
                graph.toStdString(),functionResolver(root));
            (void)northstarcpp::shaders::material_shader_prefix(
                hlsl.data(),static_cast<unsigned>(hlsl.size()),0,0.5f);
            return {};
        }catch(std::exception const& e) {
            return QString::fromUtf8(e.what());
        }
    }));
}
void MaterialGpuPreview::applyPreflight(
    QJsonObject const& descriptor,QString const& error) {
    if(!error.isEmpty()) {
        status_->setText("Graph compiler rejected: "+error);
        status_->setStyleSheet("color:#e8a1a1;");
        emit shaderDiagnostic(error);
        return;
    }
    QString diagnostic;
    if(!stage(descriptor,&diagnostic)) {
        status_->setText("Material compilation rejected: "+diagnostic);
        status_->setStyleSheet("color:#e8a1a1;");
        return;
    }
    if(!started_) {
        start(); // A formerly invalid graph may become valid after editing.
        return;
    }
    if(!viewport_->gpuActive())return;
    // Re-mount asset sources to invalidate the current renderer's material
    // cache. This is the SAME engine.render path as Player, not QPainter.
    viewport_->setAssetSources(diskPath(scratch_.path()));
    bool const drawn=viewport_->renderGpuNow();
    status_->setText(drawn?diagnostic:
        "Material staged, but native GPU draw failed. Check renderer diagnostics.");
    status_->setStyleSheet(drawn?"color:#a6d3bc;":"color:#e8a1a1;");
}
} // namespace northstarcpp::editor
