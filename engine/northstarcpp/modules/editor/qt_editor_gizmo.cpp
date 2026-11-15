#include "qt_editor_gizmo.hpp"
#include "editor_tools.hpp"
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <limits>

namespace northstarcpp::editor {
namespace {
constexpr float pi=3.14159265358979323846f;
constexpr int ring_segments=64;
constexpr double screen_limit=100000.0;
Vector3 add(Vector3 a,Vector3 b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vector3 sub(Vector3 a,Vector3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vector3 mul(Vector3 a,float b) {return {a.x*b,a.y*b,a.z*b};}
Vector3 cross(Vector3 a,Vector3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
float dot(Vector3 a,Vector3 b) {
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
Vector3 normalized(Vector3 a) {
    auto const len=std::sqrt(dot(a,a));
    return len>1.e-6f?mul(a,1.f/len):Vector3{0,0,0};
}
bool valid(QPointF p) {
    return std::isfinite(p.x()) && std::isfinite(p.y()) &&
           std::abs(p.x())<screen_limit && std::abs(p.y())<screen_limit;
}
QPointF invalid_point() {
    auto const nan=std::numeric_limits<double>::quiet_NaN();
    return {nan,nan};
}
QPointF unit(QPointF p) {
    auto const length=std::hypot(p.x(),p.y());
    return length>1.e-5? QPointF(p.x()/length,p.y()/length):QPointF();
}
double distanceToSegment(QPointF pointer,QPointF a,QPointF b,
                         QPointF* tangent) {
    auto const d=b-a;
    auto const squared=QPointF::dotProduct(d,d);
    if(squared<1.e-6)return 1.e9;
    auto const parameter=std::clamp(QPointF::dotProduct(pointer-a,d)/squared,
                                   0.0,1.0);
    auto const closest=a+d*parameter;
    if(tangent)*tangent=unit(d);
    return std::hypot(pointer.x()-closest.x(),pointer.y()-closest.y());
}
QColor axisColor(int axis,bool highlighted) {
    if(highlighted)return QColor("#ffd06c");
    if(axis==0)return QColor("#fa6676");
    if(axis==1)return QColor("#65e8a5");
    return QColor("#69aaff");
}
}
GizmoScreenGeometry makeGizmoGeometry(
    editor_tools::EditorCamera const& camera,
    SceneObject const& object,QSize viewport,int mode,bool local)
{
    GizmoScreenGeometry shape;
    if(viewport.width()<12 || viewport.height()<12 || mode<0 || mode>2)
        return shape;
    auto const center=camera.project(object.position,
        float(viewport.width()),float(viewport.height()));
    auto const depth=dot(sub(object.position,camera.pose.position),
                         camera.forward());
    if(!center || depth<=camera.pose.near_plane*1.1f ||
       !std::isfinite(depth) || !std::isfinite(center->x) ||
       !std::isfinite(center->y))return shape;
    shape.center={center->x,center->y};
    if(!valid(shape.center))return shape;
    auto const fov=std::clamp(camera.pose.fov_y_degrees,10.f,135.f)*
        pi/180.f;
    shape.world_length=std::clamp(
        88.f*2.f*std::tan(fov*.5f)*depth/float(viewport.height()),
        .025f,10000.f);
    shape.visible=true;
    for(int index=0;index<3;++index) {
        auto const axis=editor_tools::gizmo_axis(object,
            static_cast<editor_tools::GizmoAxis>(index),local);
        auto const endpoint=add(object.position,mul(axis,shape.world_length));
        if(auto const screen=camera.project(endpoint,float(viewport.width()),
                                            float(viewport.height()))) {
            shape.tips[index]={screen->x,screen->y};
            shape.axis_visible[index]=valid(shape.tips[index]) &&
                std::hypot((shape.tips[index]-shape.center).x(),
                           (shape.tips[index]-shape.center).y())>=12.;
        }
        if(mode!=1)continue;
        auto const seed=std::abs(axis.y)>.9f?Vector3{0,0,1}:Vector3{0,1,0};
        auto const u=normalized(cross(axis,seed));
        auto const v=normalized(cross(axis,u));
        auto& points=shape.rings[index];
        points.reserve(ring_segments+1);
        for(int i=0;i<=ring_segments;++i) {
            auto angle=float(i)*2.f*pi/float(ring_segments);
            auto const world=add(object.position,
                mul(add(mul(u,std::cos(angle)),mul(v,std::sin(angle))),
                    shape.world_length*.84f));
            auto const projected=camera.project(world,
                float(viewport.width()),float(viewport.height()));
            QPointF point=projected?
                QPointF(projected->x,projected->y):invalid_point();
            points.append(valid(point)?point:invalid_point());
        }
    }
    return shape;
}
GizmoHit pickGizmo(GizmoScreenGeometry const& geometry,
                   QPointF pointer,int mode,float threshold) {
    GizmoHit chosen;
    if(!geometry.visible || !valid(pointer) ||
       mode<0 || mode>2 || !std::isfinite(threshold))
        return chosen;
    auto best=std::clamp(double(threshold),4.,32.);
    for(int axis=0;axis<3;++axis) {
        QPointF tangent;
        double distance=1.e9;
        auto const tip=geometry.tips[axis];
        auto const radius=std::hypot((tip-geometry.center).x(),
                                     (tip-geometry.center).y());
        if(mode==1) {
            auto const& ring=geometry.rings[axis];
            for(int n=1;n<ring.size();++n) {
                if(!valid(ring[n-1])||!valid(ring[n]))continue;
                QPointF candidate;
                double const d=distanceToSegment(pointer,ring[n-1],ring[n],&candidate);
                if(d<distance) {distance=d;tangent=candidate;}
            }
        }else if(geometry.axis_visible[axis] && radius>12.) {
            auto const end=geometry.center+(tip-geometry.center)*.85;
            auto const start=geometry.center+(tip-geometry.center)*.17;
            distance=distanceToSegment(pointer,start,end,&tangent);
            // The arrowhead and square scale tip are also pick targets.
            double const endDistance=std::hypot(pointer.x()-tip.x(),
                                                 pointer.y()-tip.y());
            double const tipRadius=mode==2?11.:13.;
            if(endDistance<tipRadius)distance=0.;
        }
        if(distance<best) {
            best=distance;
            chosen.axis=axis;
            chosen.tangent=tangent;
            chosen.pixel_length=float(radius);
        }
    }
    return chosen;
}
void paintGizmo(QPainter& painter,GizmoScreenGeometry const& geometry,
                int mode,int selected_axis,int hovered_axis) {
    if(!geometry.visible || mode<0 || mode>2)return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing,true);
    for(int axis=0;axis<3;++axis) {
        bool const active=axis==selected_axis || axis==hovered_axis;
        QColor const color=axisColor(axis,active);
        painter.setPen(QPen(QColor("#0b1220"),active?7.:5.,
                            Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath path;
        if(mode==1) {
            bool connected=false;
            for(auto point:geometry.rings[axis]) {
                if(!valid(point)) {connected=false;continue;}
                if(!connected) {path.moveTo(point);connected=true;}
                else path.lineTo(point);
            }
        }else if(geometry.axis_visible[axis]) {
            path.moveTo(geometry.center);
            path.lineTo(geometry.tips[axis]);
        }
        painter.drawPath(path);
        painter.setPen(QPen(color,active?3.7:2.8,
                            Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter.drawPath(path);
        if(mode==1 || !geometry.axis_visible[axis])continue;
        auto const tip=geometry.tips[axis];
        auto const along=unit(tip-geometry.center);
        if(mode==0) {
            auto const orthogonal=QPointF(-along.y(),along.x());
            QPolygonF triangle;
            triangle<<tip<<(tip-along*17.+orthogonal*7.5)
                <<(tip-along*17.-orthogonal*7.5);
            painter.setPen(QPen(QColor("#0b1220"),1.5));
            painter.setBrush(color);
            painter.drawPolygon(triangle);
        }else {
            QRectF rect(tip-QPointF(7,7),QSizeF(14,14));
            painter.setBrush(color);
            painter.setPen(QPen(QColor("#0b1220"),2));
            painter.drawRect(rect);
        }
        painter.setPen(QPen(color,1));
        painter.setFont(QFont("Segoe UI",9,QFont::Bold));
        painter.drawText(tip+along*15.+QPointF(-4,4),
            axis==0?"X":axis==1?"Y":"Z");
    }
    painter.setBrush(QColor("#ecf0f3"));
    painter.setPen(QPen(QColor("#1b2633"),1.5));
    painter.drawEllipse(geometry.center,4.0,4.0);
    painter.restore();
}
float gizmoDragAmount(GizmoHit const& handle,QPointF screen_delta,
                      float world_length,int mode) {
    if(handle.axis<0 || handle.axis>2 ||
       !std::isfinite(screen_delta.x()) ||
       !std::isfinite(screen_delta.y()) ||
       !std::isfinite(world_length) || world_length<=0.f)
        return 0.f;
    auto const signed_pixels=QPointF::dotProduct(screen_delta,
                                                handle.tangent);
    if(mode==0)
        return float(signed_pixels)*world_length/
            std::max(12.f,handle.pixel_length);
    if(mode==1)
        return float(signed_pixels)*180.f/
            (pi*std::max(22.f,handle.pixel_length*.84f));
    if(mode==2)
        return float(signed_pixels)*.012f;
    return 0.f;
}
} // namespace northstarcpp::editor
