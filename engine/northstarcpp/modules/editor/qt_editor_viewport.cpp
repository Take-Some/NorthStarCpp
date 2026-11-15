#include "qt_editor_viewport.hpp"
#include "editor_tools.hpp"
#include <northstarcpp/collision_service.hpp>
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
#include <stdexcept>
#ifdef _WIN32
#include "../viewport/viewport.hpp"
#endif
namespace northstarcpp::editor {
#ifdef _WIN32
// Native Qt child HWND dedicated to the engine swapchain. The parent stays
// a normal Qt QWidget and remains responsible for input, DnD and toolbars.
class GpuSurface final : public QWidget {
public:
    explicit GpuSurface(QWidget* parent):QWidget(parent) {
        setAttribute(Qt::WA_NativeWindow,true);
        setAttribute(Qt::WA_PaintOnScreen,true);
        setAttribute(Qt::WA_NoSystemBackground,true);
        setAcceptDrops(true);
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
    });
    fly_timer_=new QTimer(this);
    fly_timer_->setTimerType(Qt::PreciseTimer);
    fly_timer_->setInterval(16); // time-integrated input independent of FPS
    connect(fly_timer_,&QTimer::timeout,this,[this] {
        if(!rotating_) {fly_timer_->stop();held_keys_.clear();return;}
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
void QtEditorViewport::setAssetSources(std::filesystem::path root) {
    asset_root_=std::move(root);
#ifdef _WIN32
    if(gpu_ && !asset_root_.empty()) {
        try {gpu_->renderer->set_asset_sources(asset_root_);}
        catch(std::exception const& e) {emit gpuUnavailable(QString::fromUtf8(e.what()));}
    }
#endif
    gpu_dirty_=true;
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
        gizmo.active_axis=drag_axis_;
        gpu_->renderer->draw(*scene_,selected_,false,nullptr,nullptr,false,
                             &pose,nullptr,false,false,&gizmo);
        gpu_dirty_=false;
    } catch(std::exception const& e) {
        auto error=QString::fromUtf8(e.what());
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
    QWidget::resizeEvent(event);
    if(gpu_surface_)gpu_surface_->setGeometry(rect());
    gpu_dirty_=true;
}
void QtEditorViewport::setScene(Scene const* scene) {
    scene_=scene;
    gpu_dirty_=true;
    update();
}
void QtEditorViewport::setCameraPose(CameraPose pose) {
    if(!std::isfinite(pose.position.x)||!std::isfinite(pose.position.y)||
       !std::isfinite(pose.position.z)||!std::isfinite(pose.yaw)||
       !std::isfinite(pose.pitch))return;
    pose.pitch=std::clamp(pose.pitch,-1.52f,1.52f);
    pose.position.x=std::clamp(pose.position.x,-900000.f,900000.f);
    pose.position.y=std::clamp(pose.position.y,-900000.f,900000.f);
    pose.position.z=std::clamp(pose.position.z,-900000.f,900000.f);
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
    selected_=index;gpu_dirty_=true;update();
}
void QtEditorViewport::setGizmoMode(int index) {
    gizmo_=std::clamp(index,0,2);gpu_dirty_=true;update();
}
void QtEditorViewport::setDrawMode(DrawMode mode) {
    if(int(mode)<0 || int(mode)>3)return;
    if(mode==DrawMode::EngineGpu) {
        try {ensureGpu();}
        catch(std::exception const& e) {
            gpu_.reset();
            if(gpu_surface_)gpu_surface_->hide();
            mode=DrawMode::Shaded;
            emit gpuUnavailable(QString::fromUtf8(e.what()));
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
void QtEditorViewport::setShowGrid(bool enabled) {show_grid_=enabled;update();}
void QtEditorViewport::setFlySpeed(float value) {
    if(!std::isfinite(value))return;
    auto const next=std::clamp(value,0.1f,2000.f);
    if(next==fly_speed_)return;
    fly_speed_=next;
    emit flySpeedChanged(fly_speed_);
    update();
}
void QtEditorViewport::advanceFly(float elapsedSeconds) {
    if(!rotating_ || !std::isfinite(elapsedSeconds) || elapsedSeconds<=0.f)return;
    float const sideways=float(held_keys_.contains(Qt::Key_D))-
                         float(held_keys_.contains(Qt::Key_A));
    float const forward=float(held_keys_.contains(Qt::Key_W))-
                        float(held_keys_.contains(Qt::Key_S));
    float const vertical=float(held_keys_.contains(Qt::Key_E))-
                         float(held_keys_.contains(Qt::Key_Q));
    float const length=std::sqrt(sideways*sideways+forward*forward+
                                  vertical*vertical);
    if(length<=0.f)return;
    float speed=fly_speed_;
    if(held_keys_.contains(Qt::Key_Shift))speed*=3.f;
    if(held_keys_.contains(Qt::Key_Control))speed*=.25f;
    float const step=std::min(0.1f,elapsedSeconds)*speed/length;
    camera_.fly(sideways*step,vertical*step,forward*step);
    gpu_dirty_=true;update();
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
    // Basic editor grid. This is a cross-platform authoring fallback and
    // does NOT pretend to render source materials or the game's Vulkan output.
    if(show_grid_)for(int coordinate=-100;coordinate<=100;coordinate+=5) {
        QColor grid=coordinate==0?QColor("#64819b"):QColor("#314259");
        painter.setPen(QPen(grid,coordinate==0?1.5:0.8));
        for(int pass=0;pass<2;++pass) {
            Vector3 a=pass==0?Vector3{float(coordinate),0,-100}:Vector3{-100,0,float(coordinate)};
            Vector3 b=pass==0?Vector3{float(coordinate),0,100}:Vector3{100,0,float(coordinate)};
            auto pa=projected(a),pb=projected(b);
            if(std::abs(pa.x())>50000 || std::abs(pb.x())>50000)continue;
            painter.drawLine(pa,pb);
        }
    }
    if(scene_) {
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
        if(selected_>=0 && static_cast<std::size_t>(selected_)<scene_->objects.size()) {
            auto const& selected=scene_->objects[static_cast<std::size_t>(selected_)];
            QColor colors[3]={"#f56b76","#6edc90","#65acff"};
            for(int axis=0;axis<3;++axis) {
                auto v=editor_tools::gizmo_axis(selected,
                    static_cast<editor_tools::GizmoAxis>(axis),false);
                Vector3 end{selected.position.x+v.x*2.8f,
                            selected.position.y+v.y*2.8f,
                            selected.position.z+v.z*2.8f};
                painter.setPen(QPen(colors[axis],drag_axis_==axis?5:3));
                painter.drawLine(projected(selected.position),projected(end));
                painter.drawEllipse(projected(end),4,4);
            }
        }
    }
    painter.restore();
    painter.setPen(QColor("#c1d7ec"));
    painter.setFont(QFont("Segoe UI",9));
    painter.drawText(QRect(14,10,width()-28,26),Qt::AlignLeft|Qt::AlignVCenter,
        "PERSPECTIVE  |  PROXY VIEWPORT  |  RMB + WASD FLY  |  Q/E UP-DOWN  |  MMB PAN");
    painter.setPen(QColor("#8ea6c2"));
    painter.drawText(QRect(14,height()-31,width()-28,22),
        Qt::AlignLeft|Qt::AlignVCenter,
        QString("Objects: %1  |  %2  |  Fly: %3 m/s  |  Shift ×3, Ctrl ×0.25  |  F Focus")
            .arg(scene_?scene_->objects.size():0)
            .arg(draw_mode_==DrawMode::Wireframe?"Wireframe":
                 draw_mode_==DrawMode::Shaded?"Shaded proxy":"Shaded + edges")
            .arg(fly_speed_,0,'f',1));
}
void QtEditorViewport::mousePressEvent(QMouseEvent* event) {
    previous_=event->position().toPoint();
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
        held_keys_.clear();
        fly_clock_.start();
        fly_timer_->start();
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
    if(selected_>=0 && static_cast<std::size_t>(selected_)<scene_->objects.size()) {
        auto const& o=scene_->objects[static_cast<std::size_t>(selected_)];
        float best=18;
        for(int axis=0;axis<3;++axis) {
            auto v=editor_tools::gizmo_axis(o,
                static_cast<editor_tools::GizmoAxis>(axis),false);
            auto p=projected({o.position.x+v.x*2.8f,o.position.y+v.y*2.8f,
                              o.position.z+v.z*2.8f});
            auto dist=QLineF(p,event->position()).length();
            if(dist<best){best=dist;drag_axis_=axis;}
        }
        if(drag_axis_>=0) {
            emit gizmoDragStarted();
            event->accept();
            return;
        }
    }
    auto ray=camera_.screen_ray(float(event->position().x()),
                                float(event->position().y()),
                                float(width()),float(height()));
    std::optional<std::size_t> index;
    try {
        static northstarcpp::CollisionService collision;
        static northstarcpp::GeometryService geometry;
        index=editor_tools::pick_actor_geometry(*scene_,ray,collision,geometry);
    } catch(std::exception const& exception) {
        // Do not silently select an AABB when exact mesh selection fails:
        // a disabled or invalid collision plugin must be diagnosed.
        emit gpuUnavailable(QString::fromUtf8(exception.what()));
        return;
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
        camera_.look(float(difference.x()),float(-difference.y()),.006f);
        camera_.pose.pitch=std::clamp(camera_.pose.pitch,-1.45f,1.45f);
        updateOrbitCamera();
    } else if(rotating_) {
        if(!event->buttons().testFlag(Qt::RightButton)) {
            rotating_=false;held_keys_.clear();fly_timer_->stop();
            unsetCursor();return;
        }
        camera_.look(float(difference.x()),float(-difference.y()));
        gpu_dirty_=true;update();
    } else if(panning_ && event->buttons().testFlag(Qt::MiddleButton)) {
        float const scale=fly_speed_*.002f;
        camera_.move(float(-difference.x())*scale,
                     float(difference.y())*scale,0.f);
        gpu_dirty_=true;update();
    } else if(drag_axis_>=0 && event->buttons().testFlag(Qt::LeftButton)) {
        float distance=float(difference.x()-difference.y())*0.035f;
        if(std::abs(distance)>0.0001f)emit gizmoDelta(gizmo_,drag_axis_,distance);
        update();
    }
}
void QtEditorViewport::mouseReleaseEvent(QMouseEvent* event) {
    if(orbit_mode_ && (event->button()==Qt::LeftButton ||
                       event->button()==Qt::RightButton)) {
        rotating_=false;unsetCursor();event->accept();return;
    }
    if(event->button()==Qt::RightButton) {
        rotating_=false;held_keys_.clear();fly_timer_->stop();unsetCursor();
        event->accept();
    }
    if(event->button()==Qt::MiddleButton) {panning_=false;unsetCursor();}
    if(event->button()==Qt::LeftButton) {
        if(drag_axis_>=0)emit gizmoDragFinished();
        drag_axis_=-1;
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
        rotating_=false;held_keys_.clear();fly_timer_->stop();
        unsetCursor();event->accept();return;
    }
    if(!rotating_ && !event->isAutoRepeat() && event->modifiers()==Qt::NoModifier) {
        if(event->key()==Qt::Key_W || event->key()==Qt::Key_E ||
           event->key()==Qt::Key_R) {
            emit gizmoModeRequested(event->key()==Qt::Key_W?0:
                                   event->key()==Qt::Key_E?1:2);
            event->accept();
            return;
        }
        if(event->key()==Qt::Key_F) {
            frameSelected();
            event->accept();
            return;
        }
    }
    if(rotating_ && !event->isAutoRepeat()) {
        switch(event->key()) {
        case Qt::Key_W:case Qt::Key_A:case Qt::Key_S:case Qt::Key_D:
        case Qt::Key_Q:case Qt::Key_E:
        case Qt::Key_Shift:case Qt::Key_Control:
            held_keys_.insert(event->key());
            event->accept();return;
        default:break;
        }
    }
    QWidget::keyPressEvent(event);
}
void QtEditorViewport::keyReleaseEvent(QKeyEvent* event) {
    if(!event->isAutoRepeat() && held_keys_.contains(event->key())) {
        held_keys_.remove(event->key());
        event->accept();return;
    }
    QWidget::keyReleaseEvent(event);
}
void QtEditorViewport::focusOutEvent(QFocusEvent* event) {
    if(drag_axis_>=0)emit gizmoDragFinished();
    rotating_=false;panning_=false;drag_axis_=-1;
    held_keys_.clear();
    if(fly_timer_)fly_timer_->stop();
    unsetCursor();
    QWidget::focusOutEvent(event);
}
void QtEditorViewport::dragEnterEvent(QDragEnterEvent* event) {
    if(event->mimeData()->hasFormat("application/x-northstar-asset-reference"))
        event->acceptProposedAction();
    else event->ignore();
}
void QtEditorViewport::dragMoveEvent(QDragMoveEvent* event) {
    if(event->mimeData()->hasFormat("application/x-northstar-asset-reference"))
        event->acceptProposedAction();
    else event->ignore();
}
void QtEditorViewport::dropEvent(QDropEvent* event) {
    if(!event->mimeData()->hasFormat("application/x-northstar-asset-reference")) {
        event->ignore();return;
    }
    auto const reference=QString::fromUtf8(
        event->mimeData()->data("application/x-northstar-asset-reference")).trimmed();
    if(reference.isEmpty()) {event->ignore();return;}
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
    emit assetDropped(reference,QVector3D(point.x,point.y,point.z),target);
    event->acceptProposedAction();
}
} // namespace northstarcpp::editor
