#include "qt_editor_viewport.hpp"
#include "editor_tools.hpp"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
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
namespace northstarcpp::editor {
QtEditorViewport::QtEditorViewport(QWidget* parent):QWidget(parent) {
    setObjectName("sceneViewport");
    setMinimumSize(300,240);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}
void QtEditorViewport::setScene(Scene const* scene) {scene_=scene;update();}
void QtEditorViewport::setSelected(int index) {selected_=index;update();}
void QtEditorViewport::setGizmoMode(int index) {gizmo_=std::clamp(index,0,2);update();}
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
    if(scene_ && selected_>=0 && std::size_t(selected_)<scene_->objects.size())
        camera_.frame(scene_->objects[static_cast<std::size_t>(selected_)].position,12.f);
    else camera_.frame({0,0,0},18.f);
    update();
}
void QtEditorViewport::paintEvent(QPaintEvent*) {
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
    for(int coordinate=-100;coordinate<=100;coordinate+=5) {
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
        for(auto const& item:visible) {
            auto const& object=scene_->objects[item.index];
            float h=object.size*0.5f;
            if(scene_->version>=3)h*=std::max({object.node.scale.x,
                object.node.scale.y,object.node.scale.z,0.1f});
            auto const p=object.position;
            QPointF vertices[8];
            for(int i=0;i<8;++i) {
                auto projectedPoint=Vector3{p.x+((i&1)?h:-h),
                                            p.y+((i&4)?h:-h),
                                            p.z+((i&2)?h:-h)};
                vertices[i]=projected(projectedPoint);
            }
            bool chosen=selected_==static_cast<int>(item.index);
            QColor shade=QColor::fromRgb(object.color);
            painter.setPen(QPen(chosen?QColor("#ffc95b"):shade.lighter(120),
                                chosen?2.3:1.0));
            for(auto const& edge:edges)
                painter.drawLine(vertices[edge.first],vertices[edge.second]);
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
        "PERSPECTIVE  |  C++ AUTHORING PREVIEW  |  RMB Orbit  |  Wheel Zoom  |  W/E/R Gizmo");
    painter.setPen(QColor("#8ea6c2"));
    painter.drawText(QRect(14,height()-31,width()-28,22),
        Qt::AlignLeft|Qt::AlignVCenter,
        QString("Objects: %1  |  Preview: proxy geometry (GPU render integration pending)")
            .arg(scene_?scene_->objects.size():0));
}
void QtEditorViewport::mousePressEvent(QMouseEvent* event) {
    previous_=event->position().toPoint();
    if(event->button()==Qt::RightButton) {
        rotating_=true;setCursor(Qt::ClosedHandCursor);return;
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
        if(drag_axis_>=0)return;
    }
    auto ray=camera_.screen_ray(float(event->position().x()),
                                float(event->position().y()),
                                float(width()),float(height()));
    auto index=editor_tools::pick_actor(*scene_,ray);
    selected_=index?static_cast<int>(*index):-1;
    emit selectionChanged(selected_);
    update();
}
void QtEditorViewport::mouseMoveEvent(QMouseEvent* event) {
    auto current=event->position().toPoint();
    auto difference=current-previous_;
    previous_=current;
    if(rotating_) {
        camera_.look(float(difference.x()),float(-difference.y()));
        update();
    } else if(drag_axis_>=0 && event->buttons().testFlag(Qt::LeftButton)) {
        float distance=float(difference.x()-difference.y())*0.035f;
        if(std::abs(distance)>0.0001f)emit gizmoDelta(gizmo_,drag_axis_,distance);
        update();
    }
}
void QtEditorViewport::mouseReleaseEvent(QMouseEvent* event) {
    if(event->button()==Qt::RightButton) {rotating_=false;unsetCursor();}
    if(event->button()==Qt::LeftButton)drag_axis_=-1;
}
void QtEditorViewport::wheelEvent(QWheelEvent* event) {
    camera_.dolly(float(event->angleDelta().y())/120.f*0.8f);
    update();
}
void QtEditorViewport::dragEnterEvent(QDragEnterEvent* event) {
    if(event->mimeData()->hasFormat("application/x-northstar-asset-reference"))
        event->acceptProposedAction();
}
void QtEditorViewport::dropEvent(QDropEvent* event) {
    if(!event->mimeData()->hasFormat("application/x-northstar-asset-reference"))
        return;
    auto raw=event->mimeData()->data("application/x-northstar-asset-reference");
    auto reference=QString::fromUtf8(raw).trimmed();
    if(reference.isEmpty())return;
    auto point=groundAt(event->position().toPoint());
    emit assetDropped(reference,QVector3D(point.x,point.y,point.z));
    event->acceptProposedAction();
}
}
