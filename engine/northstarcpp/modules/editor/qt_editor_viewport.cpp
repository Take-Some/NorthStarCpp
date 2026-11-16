#include "qt_editor_viewport.hpp"
#include "../viewport/editor_grid.hpp"
#include "editor_tools.hpp"
#include "editor_visual_prefab.hpp"
#include <northstarcpp/collision_service.hpp>
#include <northstarcpp/collision_wireframe.hpp>
#include <northstarcpp/player_character_components.hpp>
#include <northstarcpp/geometry_service.hpp>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QTimer>
#include <QResizeEvent>
#include <QCoreApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QPaintEngine>
#include <QPolygonF>
#include <QLineF>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>
#include <QVector3D>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <unordered_set>
#include <stdexcept>
#ifdef _WIN32
#include "../viewport/viewport.hpp"
#endif
namespace northstarcpp::editor {
namespace {
// Qt's key() uses the active keyboard layout. Windows native scan codes
// keep WASD/QE bound to the physical keys (also with Cyrillic layouts).
// Synthetic Qt tests and other platforms use their logical Qt keys.
int flyNavigationKey(QKeyEvent const* event) {
#ifdef _WIN32
    switch(event->nativeScanCode()) {
    case 0x11:return Qt::Key_W;
    case 0x1e:return Qt::Key_A;
    case 0x1f:return Qt::Key_S;
    case 0x20:return Qt::Key_D;
    case 0x10:return Qt::Key_Q;
    case 0x12:return Qt::Key_E;
    default:break;
    }
#endif
    switch(event->key()) {
    case Qt::Key_W:case Qt::Key_A:case Qt::Key_S:case Qt::Key_D:
    case Qt::Key_Q:case Qt::Key_E:
    case Qt::Key_Shift:case Qt::Key_Control:
        return event->key();
    default:return 0;
    }
}
bool flyModifiersAllowed(Qt::KeyboardModifiers modifiers) {
    return !modifiers.testFlag(Qt::AltModifier) &&
           !modifiers.testFlag(Qt::MetaModifier);
}
}
#ifdef _WIN32
// Native Qt child HWND dedicated to the engine swapchain. The parent stays
// a normal Qt QWidget and remains responsible for input, DnD and toolbars.
class GpuSurface final : public QWidget {
public:
    explicit GpuSurface(QWidget* parent):QWidget(parent) {
        setAttribute(Qt::WA_NativeWindow,true);
        setAttribute(Qt::WA_PaintOnScreen,true);
        setAttribute(Qt::WA_NoSystemBackground,true);
        // The parent viewport owns scene/asset drop dispatch. Native GPU
        // child windows must not swallow OLE/Qt drag-and-drop events.
        setAcceptDrops(false);
        setFocusPolicy(Qt::NoFocus);
    }
    QPaintEngine* paintEngine() const override {return nullptr;}
protected:
    void paintEvent(QPaintEvent*) override {}
};
struct QtEditorViewport::GpuState {
    std::unique_ptr<northstarcpp::Viewport> renderer;
    explicit GpuState(WId nativeWindow) :
        renderer(std::make_unique<northstarcpp::Viewport>(
            reinterpret_cast<HWND>(nativeWindow),false)) {}
};
#else
struct QtEditorViewport::GpuState {};
#endif
QtEditorViewport::~QtEditorViewport() {
    if(gpu_timer_)gpu_timer_->stop();
    // Destroy swapchain before its native child window is destroyed.
    gpu_.reset();
}
QtEditorViewport::QtEditorViewport(QWidget* parent):QWidget(parent) {
    setObjectName("sceneViewport");
    setMinimumSize(300,240);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    gpu_timer_=new QTimer(this);
    gpu_timer_->setInterval(16);
    connect(gpu_timer_,&QTimer::timeout,this,[this] {
        if(gpu_dirty_ && isVisible())drawGpu();
#ifdef _WIN32
        if(gpuActive()) {
            auto status=gpu_->renderer->poll_shader_reload();
            for(auto const& message:status.completed)
                emit shaderReloadResult(QString::fromStdString(message));
            if(status.pending)gpu_dirty_=true;
        }
#endif
#ifdef _WIN32
        if(pending_gpu_pick_ && gpuActive()) {
            try {
                auto const response=gpu_->renderer->poll_actor_gpu_pick(
                    *pending_gpu_pick_);
                if(!response ||
                   response->status!=northstarcpp::Viewport::PickStatus::Pending) {
                    pending_gpu_pick_.reset();
                    if(response &&
                       response->status==northstarcpp::Viewport::PickStatus::Ready) {
                        if(scene_) {
                            auto const& composed=visual_preview_scene_ ?
                                *visual_preview_scene_ : *scene_;
                            selected_=visual_prefab::authoring_index(
                                *scene_,composed,response->actor);
                            emit selectionChanged(selected_);
                        }
                        update();
                    }
                    // STALE is intentionally discarded: do not select an
                    // entity from an older camera/scene generation.
                }
            }catch(std::exception const& error) {
                pending_gpu_pick_.reset();
                emit gpuUnavailable(QString::fromUtf8(error.what()));
            }
        }
#endif
    });
    fly_timer_=new QTimer(this);
    fly_timer_->setTimerType(Qt::PreciseTimer);
    fly_timer_->setInterval(16); // time-integrated input independent of FPS
    connect(fly_timer_,&QTimer::timeout,this,[this] {
        // Unreal-style navigation: hold RMB and press WASD/QE to fly.
        // Releasing RMB stops flight immediately; Ctrl is never required.
        if(orbit_mode_ || !rotating_ || !hasFocus()) {
            fly_timer_->stop();
            held_keys_.clear();
            camera_.reset_flight();
            return;
        }
        float const dt=std::clamp(float(fly_clock_.restart_seconds()),0.f,.05f);
        advanceFly(dt);
    });
}
bool QtEditorViewport::gpuActive() const noexcept {
    return draw_mode_==DrawMode::EngineGpu && gpu_ && gpu_surface_;
}
bool QtEditorViewport::renderGpuNow() {
    if(!gpuActive())return false;
    gpu_dirty_=true;
    drawGpu();
    return gpuActive() && !gpu_dirty_;
}
QString QtEditorViewport::gpuAssetSummary() const {
#ifdef _WIN32
    if(gpuActive()) {
        auto const stats=gpu_->renderer->asset_stats();
        auto label=QString("Native meshes: %1 | Materials: %2 | Textured: %3 | "
                       "Cube primitives: %4 | Unresolved: %5")
            .arg(stats.native_meshes).arg(stats.material_batches)
            .arg(stats.textured_material_batches).arg(stats.cube_proxies)
            .arg(stats.invalid_meshes);
        auto const q=host_frame_profile_.summarize();
        if(q.samples)
            label+=QString(" | CPU draw p50/p95/p99: %1/%2/%3 ms (n=%4)")
                .arg(q.p50_ms,0,'f',2).arg(q.p95_ms,0,'f',2)
                .arg(q.p99_ms,0,'f',2).arg(q.samples);
        if(!visual_prefab_warning_.isEmpty())label+=" | "+visual_prefab_warning_;
        return label;
    }
#endif
    return gpu_error_.isEmpty()?"GPU renderer is not active":gpu_error_;
}
void QtEditorViewport::setAssetSources(std::filesystem::path root) {
    asset_root_=std::move(root);
#ifdef _WIN32
    if(gpu_ && !asset_root_.empty()) {
        try {gpu_->renderer->set_asset_sources(asset_root_);}
        catch(std::exception const& e) {emit gpuUnavailable(QString::fromUtf8(e.what()));}
    }
#endif
    refreshVisualPrefabPreview();
    gpu_dirty_=true;
}
std::size_t QtEditorViewport::queueCompiledShader(
    QString const& path,QByteArray const& spirv) {
#ifdef _WIN32
    if(!gpuActive())throw std::runtime_error("GPU viewport is not active");
    const auto* first=reinterpret_cast<std::uint8_t const*>(spirv.constData());
    std::vector<std::uint8_t> bytes(first,first+spirv.size());
    auto const accepted=gpu_->renderer->queue_fragment_shader_reload(
        std::filesystem::path(path.toStdWString()),bytes);
    if(accepted)gpu_dirty_=true;
    return accepted;
#else
    (void)path;(void)spirv;
    throw std::runtime_error("Shader hot reload requires a native GPU viewport");
#endif
}
void QtEditorViewport::ensureGpu() {
#ifdef _WIN32
    if(!gpu_surface_) {
        gpu_surface_=new GpuSurface(this);
        gpu_surface_->setObjectName("engineGpuSurface");
        gpu_surface_->setGeometry(rect());
        gpu_surface_->installEventFilter(this);
        gpu_surface_->show();
    }
    if(!gpu_) {
        host_frame_profile_.clear();
        auto renderer=std::make_unique<GpuState>(gpu_surface_->winId());
        if(!asset_root_.empty())
            renderer->renderer->set_asset_sources(asset_root_);
        gpu_=std::move(renderer);
    }
    gpu_surface_->show();
    gpu_surface_->raise();
    gpu_dirty_=true;
    if(!gpu_timer_->isActive())gpu_timer_->start();
#else
    throw std::runtime_error(
        "Native GPU viewport surface is not yet available on this platform.");
#endif
}
void QtEditorViewport::drawGpu() {
#ifdef _WIN32
    if(!gpuActive() || !scene_)return;
    try {
        auto const& pose=camera_.pose;
        northstarcpp::ViewportGizmo gizmo;
        gizmo.mode=unsigned(gizmo_);
        gizmo.show_grid=show_grid_;
        gizmo.show_collisions=show_collisions_;
        gizmo.grid_width=std::max(1,width());
        gizmo.grid_height=std::max(1,height());
        gizmo.active_axis=drag_axis_>=0?drag_axis_:hovered_axis_;
        if(selected_>=0 && std::size_t(selected_)<scene_->objects.size()) {
            auto const& selected=scene_->objects[std::size_t(selected_)];
            auto const geometry=gizmoGeometry();
            if(geometry.visible)gizmo.length=geometry.world_length;
            for(int axis=0;axis<3;++axis)
                gizmo.axes[axis]=editor_tools::gizmo_axis(selected,
                    static_cast<editor_tools::GizmoAxis>(axis),gizmo_local_);
        }
        QElapsedTimer cpuDrawTimer;
        cpuDrawTimer.start();
        auto const& renderScene=visual_preview_scene_?*visual_preview_scene_:*scene_;
        gpu_->renderer->draw(renderScene,selected_,false,nullptr,nullptr,false,
                             &pose,nullptr,false,false,&gizmo);
        host_frame_profile_.sample(double(cpuDrawTimer.nsecsElapsed())/1.0e6);
        gpu_dirty_=false;
        gpu_error_.clear();
    } catch(std::exception const& e) {
        auto error=QString::fromUtf8(e.what());
        gpu_error_=error;
        gpu_timer_->stop();
        // Engine renderer can be swapped/reloaded; safely release the
        // existing swapchain before returning to cross-platform proxy.
        gpu_.reset();
        if(gpu_surface_)gpu_surface_->hide();
        draw_mode_=DrawMode::Shaded;
        emit gpuUnavailable(error);
        update();
    }
#endif
}
bool QtEditorViewport::event(QEvent* e) {
    if(e->type()==QEvent::ShortcutOverride &&
       rotating_ && !orbit_mode_) {
        auto const* key=static_cast<QKeyEvent const*>(e);
        if(flyNavigationKey(key) && flyModifiersAllowed(key->modifiers())) {
            // Prevent window/menu QActions from consuming unmodified WASD
            // while RMB is held over the scene, even in the GPU viewport.
            e->accept();
            return true;
        }
    }
    return QWidget::event(e);
}
bool QtEditorViewport::eventFilter(QObject* object,QEvent* event) {
    if(object==gpu_surface_) {
        switch(event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::DragEnter:
        case QEvent::DragMove:
        case QEvent::Drop:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ShortcutOverride:
            // Surface is aligned to the parent's (0,0), so its local
            // pointer coordinates equal authoring Viewport coordinates.
            QCoreApplication::sendEvent(this,event);
            return true;
        default:break;
        }
    }
    return QWidget::eventFilter(object,event);
}
void QtEditorViewport::resizeEvent(QResizeEvent* event) {
    pending_gpu_pick_.reset();
    QWidget::resizeEvent(event);
    if(gpu_surface_)gpu_surface_->setGeometry(rect());
    gpu_dirty_=true;
}
void QtEditorViewport::setScene(Scene const* scene) {
    pending_gpu_pick_.reset();
    scene_=scene;
    refreshVisualPrefabPreview();
    gpu_dirty_=true;
    update();
}
void QtEditorViewport::refreshVisualPrefabPreview() {
    visual_preview_scene_.reset();
    visual_prefab_warning_.clear();
    if(!scene_ || asset_root_.empty() || scene_->version!=3)return;
    // Rebuild on scene/asset root changes, not once per GPU frame.
    // The cloned scene is only a render composition: gameplay stable IDs,
    // physics and serialized project data are unaffected.
    Scene candidate=*scene_;
    bool any=false;
    for(auto const& [entityId,ref]:visual_prefab::links(*scene_)) {
        try {
            auto templateScene=visual_prefab::load(asset_root_,ref);
            visual_prefab::apply(candidate,entityId,templateScene);
            any=true;
        }catch(std::exception const& error) {
            visual_prefab_warning_=QString("Visual prefab %1: %2")
                .arg(QString::fromStdString(ref),
                     QString::fromUtf8(error.what()));
            // A missing visual prefab must not crash the GPU editor.
            std::fprintf(stderr,"[WARN] [editor.prefab] %s: %s\n",
                ref.c_str(),error.what());
        }
    }
    if(any)visual_preview_scene_=std::move(candidate);
}
void QtEditorViewport::setCameraPose(CameraPose pose) {
    if(!std::isfinite(pose.position.x)||!std::isfinite(pose.position.y)||
       !std::isfinite(pose.position.z)||!std::isfinite(pose.yaw)||
       !std::isfinite(pose.pitch))return;
    pose.pitch=std::clamp(pose.pitch,-1.52f,1.52f);
    pose.position.x=std::clamp(pose.position.x,-900000.f,900000.f);
    pose.position.y=std::clamp(pose.position.y,-900000.f,900000.f);
    pose.position.z=std::clamp(pose.position.z,-900000.f,900000.f);
    camera_.reset_flight();
    camera_.pose=pose;
    gpu_dirty_=true;update();
}
void QtEditorViewport::updateOrbitCamera() {
    auto const forward=camera_.forward();
    camera_.pose.position={
        orbit_target_.x-forward.x*orbit_distance_,
        orbit_target_.y-forward.y*orbit_distance_,
        orbit_target_.z-forward.z*orbit_distance_
    };
    gpu_dirty_=true;
    update();
}
void QtEditorViewport::setOrbitMode(bool enabled,Vector3 target,float distance) {
    if(!std::isfinite(distance) || distance<=0.f)return;
    if(!std::isfinite(target.x)||!std::isfinite(target.y)||
       !std::isfinite(target.z))return;
    orbit_mode_=enabled;
    orbit_target_=target;
    orbit_distance_=std::clamp(distance,1.1f,200.f);
    rotating_=false;
    held_keys_.clear();
    camera_.reset_flight();
    fly_timer_->stop();
    unsetCursor();
    if(orbit_mode_)updateOrbitCamera();
}
void QtEditorViewport::setOrbitAngles(float yaw,float pitch) {
    if(!std::isfinite(yaw)||!std::isfinite(pitch))return;
    camera_.pose.yaw=std::remainder(yaw,6.283185307f);
    camera_.pose.pitch=std::clamp(pitch,-1.45f,1.45f);
    if(orbit_mode_)updateOrbitCamera();
}
void QtEditorViewport::setSelected(int index) {
    pending_gpu_pick_.reset();
    selected_=index;hovered_axis_=-1;gpu_dirty_=true;update();
}
GizmoScreenGeometry QtEditorViewport::gizmoGeometry() const {
    if(!scene_ || selected_<0 ||
       std::size_t(selected_)>=scene_->objects.size())
        return {};
    return makeGizmoGeometry(camera_,scene_->objects[std::size_t(selected_)],
                             size(),gizmo_,gizmo_local_);
}
void QtEditorViewport::setGizmoLocal(bool enabled) {
    gizmo_local_=enabled;
    hovered_axis_=-1;
    gpu_dirty_=true;
    update();
}
void QtEditorViewport::setGizmoMode(int index) {
    gizmo_=std::clamp(index,0,2);
    hovered_axis_=-1;
    gpu_dirty_=true;
    update();
}
void QtEditorViewport::setDrawMode(DrawMode mode) {
    pending_gpu_pick_.reset();
    if(int(mode)<0 || int(mode)>3)return;
    if(mode==DrawMode::EngineGpu) {
        try {
            ensureGpu();
            gpu_error_.clear();
        }
        catch(std::exception const& e) {
            gpu_error_=QString::fromUtf8(e.what());
            gpu_.reset();
            if(gpu_surface_)gpu_surface_->hide();
            mode=DrawMode::Shaded;
            emit gpuUnavailable(gpu_error_);
        }
    } else {
        if(gpu_timer_)gpu_timer_->stop();
        if(gpu_surface_)gpu_surface_->hide();
        gpu_.reset();
    }
    draw_mode_=mode;
    gpu_dirty_=true;
    update();
}
void QtEditorViewport::setShowGrid(bool enabled) {
    show_grid_=enabled;gpu_dirty_=true;update();
}
void QtEditorViewport::setShowCollisions(bool enabled) {
    if(show_collisions_==enabled)return;
    show_collisions_=enabled;gpu_dirty_=true;update();
}
void QtEditorViewport::setFlySpeed(float value) {
    if(!std::isfinite(value))return;
    auto const next=std::clamp(value,0.1f,2000.f);
    if(next==fly_speed_)return;
    fly_speed_=next;
    emit flySpeedChanged(fly_speed_);
    update();
}
void QtEditorViewport::advanceFly(float elapsedSeconds) {
    if(!rotating_ || orbit_mode_ ||
       !std::isfinite(elapsedSeconds) || elapsedSeconds<=0.f)
        return;
    float const sideways=float(held_keys_.contains(Qt::Key_D))-
                         float(held_keys_.contains(Qt::Key_A));
    float const forward=float(held_keys_.contains(Qt::Key_W))-
                        float(held_keys_.contains(Qt::Key_S));
    float const vertical=float(held_keys_.contains(Qt::Key_E))-
                         float(held_keys_.contains(Qt::Key_Q));
    float speed=fly_speed_;
    if(held_keys_.contains(Qt::Key_Shift))speed*=3.f;
    if(held_keys_.contains(Qt::Key_Control))speed*=.25f;
    if(camera_.fly_damped(sideways,vertical,forward,speed,
                          std::min(0.1f,elapsedSeconds))) {
        gpu_dirty_=true;
        update();
    }
}
QPointF QtEditorViewport::projected(Vector3 point) const {
    auto position=camera_.project(point,float(width()),float(height()));
    return position?QPointF(position->x,position->y):
        QPointF(-100000.f,-100000.f);
}
Vector3 QtEditorViewport::groundAt(QPoint p) const {
    auto ray=camera_.screen_ray(float(p.x()),float(p.y()),
                                float(width()),float(height()));
    if(std::abs(ray.direction.y)>1.e-5f) {
        float t=-ray.origin.y/ray.direction.y;
        if(t>0.f && t<30000.f) {
            return {std::clamp(ray.origin.x+ray.direction.x*t,-900000.f,900000.f),
                    0.f,
                    std::clamp(ray.origin.z+ray.direction.z*t,-900000.f,900000.f)};
        }
    }
    return {0.f,0.f,0.f};
}
void QtEditorViewport::frameSelected() {
    if(!scene_ || selected_<0 ||
       std::size_t(selected_)>=scene_->objects.size()) {
        frameAll();
        return;
    }
    auto const& object=scene_->objects[std::size_t(selected_)];
    auto size=std::max(.01f,object.size);
    if(scene_->version==3)
        size*=std::max({std::abs(object.node.scale.x),
                         std::abs(object.node.scale.y),
                         std::abs(object.node.scale.z),.01f});
    Vector3 const center{object.position.x,
                         object.position.y+size*.5f,object.position.z};
    // Fit the actor proxy in the smaller viewport dimension, rather than
    // placing every actor at the same hardcoded 12-meter distance.
    float const fov=std::clamp(camera_.pose.fov_y_degrees,15.f,120.f);
    float const radians=fov*3.14159265f/180.f;
    float const distance=std::max(2.5f,size*.9f/std::tan(radians*.5f));
    camera_.frame(center,distance);
    gpu_dirty_=true;update();
}
void QtEditorViewport::frameAll() {
    if(!scene_ || scene_->objects.empty()) {
        camera_.frame({0,0,0},18.f);
        update();
        return;
    }
    Vector3 lo{900000.f,900000.f,900000.f};
    Vector3 hi{-900000.f,-900000.f,-900000.f};
    for(auto const& object:scene_->objects) {
        if(scene_->version==3 && !object.node.visible)continue;
        float extent=std::max(.01f,object.size);
        if(scene_->version==3)
            extent*=std::max({std::abs(object.node.scale.x),
                              std::abs(object.node.scale.y),
                              std::abs(object.node.scale.z),.01f});
        lo.x=std::min(lo.x,object.position.x-extent);
        lo.y=std::min(lo.y,object.position.y-extent);
        lo.z=std::min(lo.z,object.position.z-extent);
        hi.x=std::max(hi.x,object.position.x+extent);
        hi.y=std::max(hi.y,object.position.y+extent);
        hi.z=std::max(hi.z,object.position.z+extent);
    }
    if(lo.x>hi.x) {camera_.frame({0,0,0},18.f);update();return;}
    Vector3 const center{(lo.x+hi.x)*.5f,
                         (lo.y+hi.y)*.5f,
                         (lo.z+hi.z)*.5f};
    float const size=std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z,1.f});
    auto const radians=std::clamp(camera_.pose.fov_y_degrees,15.f,120.f)*
                       3.14159265f/180.f;
    camera_.frame(center,std::max(10.f,
        size*.85f/std::tan(radians*.5f)));
    gpu_dirty_=true;update();
}
void QtEditorViewport::paintEvent(QPaintEvent*) {
    if(gpuActive()) {gpu_dirty_=true;return;}
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing,true);
    QLinearGradient bg(0,0,0,height());
    bg.setColorAt(0,QColor("#1d2d44"));
    bg.setColorAt(1,QColor("#0e141f"));
    painter.fillRect(rect(),bg);
    painter.save();
    painter.setClipRect(rect().adjusted(2,2,-2,-2));
    // Camera-relative infinite ground grid: adaptive 1/2/5 spacing with
    // major divisions and highlighted world axes. Only a bounded visible
    // neighbourhood is generated each frame, not fixed +/-100 metres.
    if(show_grid_) {
        auto const layout=editor_grid::build(camera_.pose,width(),height());
        auto const forward=camera_.forward();
        auto const eye=camera_.pose.position;
        auto const nearClip=std::max(.01f,camera_.pose.near_plane*1.025f);
        auto depth=[&](Vector3 p) {
            return (p.x-eye.x)*forward.x+
                   (p.y-eye.y)*forward.y+
                   (p.z-eye.z)*forward.z;
        };
        for(auto const& line:layout.lines) {
            Vector3 a=line.from,b=line.to;
            auto da=depth(a),db=depth(b);
            if(da<=nearClip && db<=nearClip)continue;
            // Clamp line endpoints to the near plane to avoid broken
            // polygons and enormous projected coordinates at the horizon.
            if(da<nearClip) {
                auto const t=(nearClip-da)/(db-da);
                a={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,
                   a.z+(b.z-a.z)*t};
            }else if(db<nearClip) {
                auto const t=(nearClip-db)/(da-db);
                b={b.x+(a.x-b.x)*t,b.y+(a.y-b.y)*t,
                   b.z+(a.z-b.z)*t};
            }
            auto const pa=camera_.project(a,float(width()),float(height()));
            auto const pb=camera_.project(b,float(width()),float(height()));
            if(!pa || !pb)continue;
            auto const begin=QPointF(pa->x,pa->y);
            auto const finish=QPointF(pb->x,pb->y);
            if(!std::isfinite(begin.x())||!std::isfinite(begin.y())||
               !std::isfinite(finish.x())||!std::isfinite(finish.y()))
                continue;
            // Keep fine lines unobtrusive, with stronger major and world
            // origin axes. Antialiasing is already enabled on QPainter.
            painter.setPen(QPen(QColor::fromRgb(line.color),
                line.origin?1.75:line.major?1.15:0.65,
                Qt::SolidLine,Qt::FlatCap));
            painter.drawLine(begin,finish);
        }
    }
    if(scene_) {
        std::unordered_set<std::uint64_t> authored_characters;
        for(auto const& c:scene_->components)
            if(c.type==player_character::pawn_type && c.entity_id)
                authored_characters.insert(c.entity_id);
        struct Draw {float distance;std::size_t index;};
        std::vector<Draw> visible;
        visible.reserve(scene_->objects.size());
        for(std::size_t i=0;i<scene_->objects.size();++i) {
            auto const& object=scene_->objects[i];
            if(scene_->version>=3 && !object.node.visible)continue;
            auto center=projected(object.position);
            if(std::abs(center.x())>10000||std::abs(center.y())>10000)continue;
            auto const d=object.position;
            auto const c=camera_.pose.position;
            visible.push_back({(d.x-c.x)*(d.x-c.x)+(d.y-c.y)*(d.y-c.y)+
                               (d.z-c.z)*(d.z-c.z),i});
        }
        std::sort(visible.begin(),visible.end(),[](Draw a,Draw b){
            return a.distance>b.distance;
        });
        // Proxy wireframe is bounded to keep a large scene responsive.
        if(visible.size()>4000)visible.resize(4000);
        constexpr std::array<std::pair<int,int>,12> edges{{{0,1},{1,3},{3,2},{2,0},
            {4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}}};
        constexpr std::array<std::array<int,4>,6> faces{{
            {{0,2,3,1}},{{4,5,7,6}},{{0,1,5,4}},
            {{2,6,7,3}},{{0,4,6,2}},{{1,3,7,5}}
        }};
        for(auto const& item:visible) {
            auto const& object=scene_->objects[item.index];
            float const size=std::max(.001f,object.size);
            auto const p=object.position;
            auto const isV3=scene_->version>=3;
            // Respect authored non-uniform scale and rotation in proxy view.
            auto const xAxis=isV3?editor_tools::gizmo_axis(object,
                editor_tools::GizmoAxis::x,true):Vector3{1.f,0.f,0.f};
            auto const yAxis=isV3?editor_tools::gizmo_axis(object,
                editor_tools::GizmoAxis::y,true):Vector3{0.f,1.f,0.f};
            auto const zAxis=isV3?editor_tools::gizmo_axis(object,
                editor_tools::GizmoAxis::z,true):Vector3{0.f,0.f,1.f};
            float const sx=isV3?size*object.node.scale.x:size;
            float const sy=isV3?size*object.node.scale.y:size;
            float const sz=isV3?size*object.node.scale.z:size;
            Vector3 const center{p.x,p.y+sy*.5f,p.z};
            QPointF screen[8];
            Vector3 world[8];
            bool hasBehind=false;
            auto const direction=camera_.forward();
            auto const cameraPosition=camera_.pose.position;
            for(int k=0;k<8;++k) {
                float x=((k&1)? .5f:-.5f)*sx;
                float y=((k&4)? .5f:-.5f)*sy;
                float z=((k&2)? .5f:-.5f)*sz;
                world[k]=Vector3{
                    center.x+xAxis.x*x+yAxis.x*y+zAxis.x*z,
                    center.y+xAxis.y*x+yAxis.y*y+zAxis.y*z,
                    center.z+xAxis.z*x+yAxis.z*y+zAxis.z*z};
                Vector3 diff{world[k].x-cameraPosition.x,
                             world[k].y-cameraPosition.y,
                             world[k].z-cameraPosition.z};
                if(diff.x*direction.x+diff.y*direction.y+
                   diff.z*direction.z<=camera_.pose.near_plane)
                    hasBehind=true;
                screen[k]=projected(world[k]);
            }
            if(hasBehind)continue; // Never project geometry behind near plane.
            if(authored_characters.count(object.node.id)) {
                // A Character is an entity with components, not a colored cube.
                // In proxy views draw a selection-safe capsule and heading.
                auto const feet=projected(p);
                auto const head=projected({p.x,p.y+std::max(1.6f,size),p.z});
                float const h=std::max(18.f,float(std::abs(feet.y()-head.y())));
                float const w=std::max(9.f,h*.24f);
                QRectF shape(head.x()-w*.5f,head.y(),w,h);
                bool const selected=static_cast<int>(item.index)==selected_;
                painter.setBrush(QColor(53,144,180,selected?105:65));
                painter.setPen(QPen(selected?QColor("#ffce68"):
                    QColor("#64c7e9"),selected?2.4:1.6));
                painter.drawRoundedRect(shape,w*.5f,w*.5f);
                painter.drawEllipse(QPointF(head.x(),head.y()+w*.32f),
                                    w*.22f,w*.22f);
                painter.drawLine(QPointF(feet.x(),feet.y()),
                    QPointF(feet.x()+w*.55f,feet.y()-w*.20f));
                if(selected) {
                    painter.setPen(QColor("#f5f0d9"));
                    painter.drawText(head+QPointF(w,-10),
                        QString::fromUtf8(object.name.c_str()));
                }
                continue;
            }
            bool const chosen=selected_==static_cast<int>(item.index);
            QColor const color=QColor::fromRgb(object.color);
            if(draw_mode_!=DrawMode::Wireframe) {
                struct Face {float depth;int index;float brightness;};
                std::array<Face,6> sortedFaces{};
                for(int face=0;face<6;++face) {
                    auto const& indexes=faces[face];
                    Vector3 const a=world[indexes[0]],b=world[indexes[1]],
                        c=world[indexes[2]];
                    Vector3 u{b.x-a.x,b.y-a.y,b.z-a.z};
                    Vector3 v{c.x-a.x,c.y-a.y,c.z-a.z};
                    Vector3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
                    float norm=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
                    float shade=.45f;
                    if(norm>.0000001f) {
                        n.x/=norm;n.y/=norm;n.z/=norm;
                        shade=std::clamp(.35f+
                            .55f*std::abs(n.x*(-.35f)+n.y*.82f+n.z*.45f),
                            .25f,1.f);
                    }
                    float depth=0.f;
                    for(int index:indexes) {
                        auto const& vtx=world[index];
                        Vector3 diff{vtx.x-cameraPosition.x,
                            vtx.y-cameraPosition.y,vtx.z-cameraPosition.z};
                        depth+=diff.x*direction.x+diff.y*direction.y+
                            diff.z*direction.z;
                    }
                    sortedFaces[face]={depth*.25f,face,shade};
                }
                std::sort(sortedFaces.begin(),sortedFaces.end(),
                    [](Face lhs,Face rhs){return lhs.depth>rhs.depth;});
                for(auto const& face:sortedFaces) {
                    QPolygonF poly;
                    for(auto const index:faces[face.index])poly<<screen[index];
                    painter.setPen(draw_mode_==DrawMode::ShadedWireframe?
                        QPen(color.lighter(135),.8):Qt::NoPen);
                    QColor filled=color;
                    // QColor::lighter percentage is a stable proxy for studio
                    // directional shading, not the game's PBR shader.
                    filled=filled.darker(int(100.f/std::max(.25f,face.brightness)));
                    painter.setBrush(filled);
                    painter.drawPolygon(poly);
                }
                painter.setBrush(Qt::NoBrush);
            }
            if(draw_mode_!=DrawMode::Shaded || chosen) {
                painter.setPen(QPen(chosen?QColor("#ffc95b"):color.lighter(120),
                                    chosen?2.3:1.0));
                for(auto const& edge:edges)
                    painter.drawLine(screen[edge.first],screen[edge.second]);
            }
            if(chosen) {
                painter.setPen(QColor("#f5f0d9"));
                painter.drawText(projected(p)+QPointF(9,-12),
                                 QString::fromUtf8(object.name.c_str()));
            }
        }
        if(show_collisions_) {
            std::size_t shown=0;
            auto wire=[&](std::size_t i) {
                if(i>=scene_->objects.size()||shown>=80)return;
                auto const& actor=scene_->objects[i];
                if(!actor.node.visible)return;
                ++shown;
                painter.setPen(QPen(!actor.node.collision?QColor("#dc7075"):
                    int(i)==selected_?QColor("#ffce68"):QColor("#62e5bb"),1.5));
                collision_wireframe::draw(*scene_,actor,[&](Vector3 a,Vector3 b) {
                    auto const start=camera_.project(a,float(width()),float(height()));
                    auto const end=camera_.project(b,float(width()),float(height()));
                    if(start&&end)painter.drawLine(QPointF(start->x,start->y),
                                                  QPointF(end->x,end->y));
                },int(i)==selected_,collision_hull_preview_);
            };
            if(selected_>=0)wire(static_cast<std::size_t>(selected_));
            for(std::size_t i=0;i<scene_->objects.size()&&shown<80;++i)
                if(int(i)!=selected_)wire(i);
        }
        // Shared screen-space geometry ensures handles, hit targets and
        // GPU world-space overlay use the same camera-relative scale.
        paintGizmo(painter,gizmoGeometry(),gizmo_,drag_axis_,hovered_axis_);
    }
    painter.restore();
    painter.setPen(QColor("#c1d7ec"));
    painter.setFont(QFont("Segoe UI",9));
    painter.drawText(QRect(14,10,width()-28,26),Qt::AlignLeft|Qt::AlignVCenter,
        "PERSPECTIVE  |  PROXY VIEWPORT  |  HOLD RMB + WASD FLY  |  Q/E UP-DOWN  |  MMB PAN");
    // A proxy view must never look like a real (but broken) asset renderer.
    // Show the actual GPU failure persistently instead of only in a
    // transient status bar message, which was easy to miss.
    bool has_native_assets=false;
    if(scene_)for(auto const& actor:scene_->objects)
        if(!actor.node.asset.empty() || (scene_ && player_character::has_pawn(*scene_,actor.node.id))) {
            has_native_assets=true;
            break;
        }
    if(!gpu_error_.isEmpty() || has_native_assets) {
        auto const message=!gpu_error_.isEmpty()?
            QString("GPU VIEWPORT UNAVAILABLE — %1").arg(gpu_error_):
            QString("PROXY VIEW — Models and materials are not rendered. "
                    "Select 'Engine GPU - Native Models & Materials'.");
        QRect banner(12,40,std::max(1,width()-24),58);
        painter.setPen(QPen(QColor("#9b633f"),1));
        painter.setBrush(QColor(42,34,32,238));
        painter.drawRoundedRect(banner,5,5);
        painter.setPen(QColor("#f4cba3"));
        painter.drawText(banner.adjusted(11,5,-9,-5),
                         Qt::TextWordWrap|Qt::AlignVCenter,message);
    }
    painter.setPen(QColor("#8ea6c2"));
    painter.drawText(QRect(14,height()-31,width()-28,22),
        Qt::AlignLeft|Qt::AlignVCenter,
        QString("Objects: %1  |  %2  |  Fly: %3 m/s  |  Shift Г—3, Ctrl Г—0.25  |  F Focus")
            .arg(scene_?scene_->objects.size():0)
            .arg(draw_mode_==DrawMode::Wireframe?"Wireframe":
                 draw_mode_==DrawMode::Shaded?"Shaded proxy":"Shaded + edges")
            .arg(fly_speed_,0,'f',1));
}
void QtEditorViewport::mousePressEvent(QMouseEvent* event) {
    previous_=event->position().toPoint();
    // Capture keyboard navigation while RMB is held. The native GPU child
    // surface forwards mouse events here; no Ctrl modifier is needed.
    setFocus(Qt::MouseFocusReason);
    if(orbit_mode_ && (event->button()==Qt::LeftButton ||
                       event->button()==Qt::RightButton)) {
        rotating_=true;
        setFocus(Qt::MouseFocusReason);
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if(event->button()==Qt::RightButton) {
        setFocus(Qt::MouseFocusReason);
        rotating_=true;
        // RMB activates free-flight input. Keys are collected only while
        // this button remains pressed; keyboard focus stays on the viewport.
        held_keys_.clear();
        camera_.reset_flight();
        if(!fly_timer_->isActive()) {
            fly_clock_.start();
            fly_timer_->start();
        }
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if(event->button()==Qt::MiddleButton) {
        panning_=true;
        setCursor(Qt::SizeAllCursor);
        event->accept();return;
    }
    if(event->button()!=Qt::LeftButton || !scene_)return;
    if(selected_>=0 && std::size_t(selected_)<scene_->objects.size()) {
        auto const geometry=gizmoGeometry();
        auto const picked=pickGizmo(geometry,event->position(),gizmo_);
        if(picked.axis>=0) {
            drag_axis_=picked.axis;
            hovered_axis_=picked.axis;
            drag_handle_=picked;
            drag_world_length_=geometry.world_length;
            setCursor(Qt::SizeAllCursor);
            gpu_dirty_=true;
            update();
            emit gizmoDragStarted();
            event->accept();
            return;
        }
    }
    std::optional<std::size_t> index;
    bool used_gpu_id=false;
#ifdef _WIN32
    if(gpuActive() && event->position().x()>=0 &&
       event->position().y()>=0) {
        if(gpu_dirty_)drawGpu();
        if(gpuActive()) {
            try {
                // Qt pointer events are logical pixels while the native
                // Vulkan/D3D12 swapchain uses HWND client pixels. Scaling by
                // the real client rect also handles noninteger DPI factors.
                auto screen_x=event->position().x();
                auto screen_y=event->position().y();
                if(gpu_surface_) {
                    RECT native{};
                    if(GetClientRect(reinterpret_cast<HWND>(gpu_surface_->winId()),
                                     &native) &&
                       native.right>0 && native.bottom>0 &&
                       gpu_surface_->width()>0 && gpu_surface_->height()>0) {
                        screen_x*=double(native.right)/gpu_surface_->width();
                        screen_y*=double(native.bottom)/gpu_surface_->height();
                        screen_x=std::clamp(screen_x,0.,double(native.right-1));
                        screen_y=std::clamp(screen_y,0.,double(native.bottom-1));
                    }
                }
                if(gpu_->renderer->supports_async_gpu_pick()) {
                    auto const request=gpu_->renderer->enqueue_actor_gpu_pick(
                        static_cast<std::uint32_t>(screen_x),
                        static_cast<std::uint32_t>(screen_y));
                    if(request) {
                        pending_gpu_pick_=*request;
                        event->accept();
                        return; // complete selection on the next GPU fence poll
                    }
                    // Bounded ring saturated or unsupported scene: exact CPU
                    // mesh raycast, never the synchronous GPU readback.
                } else {
                    auto const picked=gpu_->renderer->pick_actor_gpu(
                        static_cast<std::uint32_t>(screen_x),
                        static_cast<std::uint32_t>(screen_y));
                    if(picked) {
                        used_gpu_id=true;
                        if(scene_) {
                            auto const& composed=visual_preview_scene_ ?
                                *visual_preview_scene_ : *scene_;
                            auto owner=visual_prefab::authoring_index(
                                *scene_,composed,*picked);
                            if(owner>=0)index=static_cast<std::size_t>(owner);
                        }
                    }
                }
            }catch(std::exception const& exception) {
                emit gpuUnavailable(QString::fromUtf8(exception.what()));
            }
        }
    }
#endif
    if(!used_gpu_id) {
        auto ray=camera_.screen_ray(float(event->position().x()),
                                    float(event->position().y()),
                                    float(width()),float(height()));
        try {
            static northstarcpp::CollisionService collision;
            static northstarcpp::GeometryService geometry;
            auto const& composed=visual_preview_scene_ ?
                *visual_preview_scene_ : *scene_;
            auto const picked=editor_tools::pick_actor_geometry(
                composed,ray,collision,geometry);
            if(picked) {
                auto owner=visual_prefab::authoring_index(
                    *scene_,composed,static_cast<int>(*picked));
                if(owner>=0)index=static_cast<std::size_t>(owner);
            }
        } catch(std::exception const& exception) {
            // Exact mesh picking remains mandatory when an ID attachment is
            // absent; do not silently fall back to approximate AABBs.
            emit gpuUnavailable(QString::fromUtf8(exception.what()));
            return;
        }
    }
    selected_=index?static_cast<int>(*index):-1;
    emit selectionChanged(selected_);
    update();
}
void QtEditorViewport::mouseMoveEvent(QMouseEvent* event) {
    auto current=event->position().toPoint();
    auto difference=current-previous_;
    previous_=current;
    if(orbit_mode_ && rotating_) {
        if(!(event->buttons()&(Qt::LeftButton|Qt::RightButton))) {
            rotating_=false;unsetCursor();return;
        }
        camera_.drag_look(float(difference.x()),float(difference.y()),.006f);
        camera_.pose.pitch=std::clamp(camera_.pose.pitch,-1.45f,1.45f);
        updateOrbitCamera();
    } else if(rotating_) {
        if(!event->buttons().testFlag(Qt::RightButton)) {
            rotating_=false;unsetCursor();return;
        }
        camera_.drag_look(float(difference.x()),float(difference.y()));
        gpu_dirty_=true;update();
    } else if(panning_ && event->buttons().testFlag(Qt::MiddleButton)) {
        float const scale=fly_speed_*.002f;
        camera_.move(float(-difference.x())*scale,
                     float(difference.y())*scale,0.f);
        gpu_dirty_=true;update();
    } else if(drag_axis_>=0 && event->buttons().testFlag(Qt::LeftButton)) {
        float const amount=gizmoDragAmount(drag_handle_,
            QPointF(difference),drag_world_length_,gizmo_);
        if(std::isfinite(amount) && std::abs(amount)>1.e-6f)
            emit gizmoDelta(gizmo_,drag_axis_,amount);
        gpu_dirty_=true;
        update();
    } else if(!rotating_ && !panning_ && drag_axis_<0 &&
              event->buttons()==Qt::NoButton) {
        auto const hovered=pickGizmo(gizmoGeometry(),event->position(),
                                     gizmo_).axis;
        if(hovered!=hovered_axis_) {
            hovered_axis_=hovered;
            setCursor(hovered_axis_>=0?Qt::SizeAllCursor:Qt::ArrowCursor);
            gpu_dirty_=true;
            update();
        }
    }
}
void QtEditorViewport::mouseReleaseEvent(QMouseEvent* event) {
    if(orbit_mode_ && (event->button()==Qt::LeftButton ||
                       event->button()==Qt::RightButton)) {
        rotating_=false;unsetCursor();event->accept();return;
    }
    if(event->button()==Qt::RightButton) {
        rotating_=false;
        held_keys_.clear();
        camera_.reset_flight();
        fly_timer_->stop();
        unsetCursor();
        event->accept();
    }
    if(event->button()==Qt::MiddleButton) {panning_=false;unsetCursor();}
    if(event->button()==Qt::LeftButton) {
        if(drag_axis_>=0) {
            emit gizmoDragFinished();
            drag_axis_=-1;
            drag_handle_={};
            drag_world_length_=0.f;
            hovered_axis_=pickGizmo(gizmoGeometry(),event->position(),gizmo_).axis;
            setCursor(hovered_axis_>=0?Qt::SizeAllCursor:Qt::ArrowCursor);
            gpu_dirty_=true;
            update();
            event->accept();
        }
    }
}
void QtEditorViewport::wheelEvent(QWheelEvent* event) {
    float const notches=float(event->angleDelta().y())/120.f;
    if(orbit_mode_) {
        orbit_distance_=std::clamp(orbit_distance_*std::pow(.88f,notches),
                                   1.1f,200.f);
        updateOrbitCamera();
        event->accept();return;
    }
    if(rotating_)setFlySpeed(fly_speed_*std::pow(1.15f,notches));
    else camera_.dolly(notches*std::clamp(fly_speed_*.18f,.1f,75.f));
    gpu_dirty_=true;update();
    event->accept();
}
void QtEditorViewport::keyPressEvent(QKeyEvent* event) {
    if(event->key()==Qt::Key_Escape && rotating_) {
        rotating_=false;
        held_keys_.clear();
        camera_.reset_flight();
        fly_timer_->stop();
        unsetCursor();
        event->accept();
        return;
    }
    // Hold RMB to fly. Plain WASD/QE works; Shift and Ctrl only modify speed.
    if(rotating_ && !orbit_mode_ && flyModifiersAllowed(event->modifiers())) {
        auto const key=flyNavigationKey(event);
        if(key) {
            if(!event->isAutoRepeat())held_keys_.insert(key);
            event->accept();
            return;
        }
    }
    if(!rotating_ && !event->isAutoRepeat() &&
       event->modifiers()==Qt::NoModifier) {
        if(event->key()==Qt::Key_R) {
            emit gizmoModeRequested(2);
            event->accept();return;
        }
        if(event->key()==Qt::Key_F) {
            frameSelected();
            event->accept();return;
        }
    }
    QWidget::keyPressEvent(event);
}
void QtEditorViewport::keyReleaseEvent(QKeyEvent* event) {
    auto const key=flyNavigationKey(event);
    if(key && held_keys_.contains(key)) {
        if(!event->isAutoRepeat())held_keys_.remove(key);
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}
void QtEditorViewport::focusOutEvent(QFocusEvent* event) {
    if(drag_axis_>=0)emit gizmoDragFinished();
    rotating_=false;panning_=false;drag_axis_=-1;hovered_axis_=-1;
    drag_handle_={};drag_world_length_=0.f;
    held_keys_.clear();
    camera_.reset_flight();
    if(fly_timer_)fly_timer_->stop();
    unsetCursor();
    QWidget::focusOutEvent(event);
}
void QtEditorViewport::dragEnterEvent(QDragEnterEvent* event) {
    if(event->mimeData()->hasFormat("application/x-northstar-asset-reference") ||
       event->mimeData()->hasFormat("application/x-northstar-placeable-id"))
        event->acceptProposedAction();
    else event->ignore();
}
void QtEditorViewport::dragMoveEvent(QDragMoveEvent* event) {
    if(event->mimeData()->hasFormat("application/x-northstar-asset-reference") ||
       event->mimeData()->hasFormat("application/x-northstar-placeable-id"))
        event->acceptProposedAction();
    else event->ignore();
}
void QtEditorViewport::dropEvent(QDropEvent* event) {
    auto const placeable=event->mimeData()->hasFormat(
        "application/x-northstar-placeable-id");
    if(!placeable &&
       !event->mimeData()->hasFormat("application/x-northstar-asset-reference")) {
        event->ignore();return;
    }
    auto const reference=QString::fromUtf8(event->mimeData()->data(
        placeable?"application/x-northstar-placeable-id":
                  "application/x-northstar-asset-reference")).trimmed();
    if(reference.isEmpty() || reference.size()>256 ||
       reference.contains('/') || reference.contains('\\')) {
        if(placeable) {event->ignore();return;}
        if(reference.isEmpty()) {event->ignore();return;}
    }
    auto const ray=camera_.screen_ray(float(event->position().x()),
        float(event->position().y()),float(width()),float(height()));
    auto point=groundAt(event->position().toPoint());
    int target=-1;
    if(scene_) {
        try {
            static northstarcpp::CollisionService collision;
            static northstarcpp::GeometryService geometry;
            auto hit=editor_tools::hit_actor_geometry(*scene_,ray,collision,geometry);
            float const surfaceDistance=hit?hit->distance:
                std::numeric_limits<float>::infinity();
            float groundDistance=std::numeric_limits<float>::infinity();
            if(std::abs(ray.direction.y)>1.e-5f)
                groundDistance=-ray.origin.y/ray.direction.y;
            // The nearest surface wins, so a ray through an actor cannot
            // accidentally place content on the floor behind it.
            if(hit && (groundDistance<0.f ||
                       surfaceDistance<=groundDistance+0.001f)) {
                target=static_cast<int>(hit->index);
                point=hit->position;
            }
        } catch(std::exception const& e) {
            emit gpuUnavailable(QString::fromUtf8(e.what()));
            event->ignore();return;
        }
    }
    if(placeable)
        emit placeableDropped(reference,QVector3D(point.x,point.y,point.z));
    else emit assetDropped(reference,QVector3D(point.x,point.y,point.z),target);
    event->acceptProposedAction();
}
} // namespace northstarcpp::editor
