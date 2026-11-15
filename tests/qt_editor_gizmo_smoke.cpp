// Native Qt gizmo acceptance: 3 distinct shapes, distance-stable
// projection, real mouse picking, axis-aligned drag and responsive input.
#include "qt_editor_viewport.hpp"
#include "qt_editor_gizmo.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace northstarcpp;
using namespace northstarcpp::editor;

void require(bool ok,char const* why) {
    if(!ok)throw std::runtime_error(why);
}
QMouseEvent mouseEvent(QEvent::Type type,QPointF position,
                      Qt::MouseButton button,Qt::MouseButtons buttons,
                      QWidget* widget) {
    return QMouseEvent(type,position,
        QPointF(widget->mapToGlobal(position.toPoint())),
        button,buttons,Qt::NoModifier);
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        editor_tools::EditorCamera camera;
        camera.pose.position={0,0,-10};
        camera.pose.yaw=camera.pose.pitch=0;
        camera.pose.fov_y_degrees=60;
        SceneObject object;
        object.position={0,0,0};
        auto const viewportSize=QSize(1200,800);
        for(int mode=0;mode<3;++mode) {
            auto const geometry=makeGizmoGeometry(camera,object,viewportSize,mode);
            require(geometry.visible,"Gizmo not projected");
            require(geometry.axis_visible[0] && geometry.axis_visible[1],
                    "Move/Scale X-Y axis endpoints invisible");
            require(geometry.tips[0].x()>geometry.center.x()+40 &&
                    geometry.tips[1].y()<geometry.center.y()-40,
                    "Gizmo screen-space axis orientation incorrect");
            QImage image(viewportSize,QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            {
                QPainter painter(&image);
                paintGizmo(painter,geometry,mode,-1,-1);
            }
            auto pixels=0;
            for(int y=0;y<image.height();y+=3)
                for(int x=0;x<image.width();x+=3)
                    if(image.pixelColor(x,y).alpha()>150)++pixels;
            require(pixels>70,"Gizmo did not render in proxy raster");
            if(mode==1) {
                auto const point=geometry.rings[2][8];
                auto const hit=pickGizmo(geometry,point,mode);
                require(hit.axis==2,"Radial rotate ring hit test failed");
                require(std::abs(gizmoDragAmount(hit,hit.tangent*14.,
                    geometry.world_length,mode))>3.f,
                    "Rotation drag has insufficient angular sensitivity");
            }else {
                auto const tip=geometry.tips[0];
                auto const pointer=geometry.center+
                    (tip-geometry.center)*(mode==0?.65:1.);
                auto const hit=pickGizmo(geometry,pointer,mode);
                require(hit.axis==0,"Move/Scale X handle hit test failed");
                require(gizmoDragAmount(hit,hit.tangent*14.,
                    geometry.world_length,mode)>0,
                    "X-axis gizmo mouse sensitivity missing");
            }
        }
        camera.pose.position={0,0,-40};
        auto distant=makeGizmoGeometry(camera,object,viewportSize,0);
        auto closeCamera=camera;closeCamera.pose.position={0,0,-10};
        auto close=makeGizmoGeometry(closeCamera,object,viewportSize,0);
        require(std::abs(std::hypot((close.tips[0]-close.center).x(),
                                    (close.tips[0]-close.center).y())-
                         std::hypot((distant.tips[0]-distant.center).x(),
                                    (distant.tips[0]-distant.center).y()))<3.,
                "Gizmo screen dimensions vary with camera distance");

        // Test actual QWidget mouse events (not only standalone geometry).
        QtEditorViewport view;
        Scene scene;
        scene.version=3;
        scene.objects.push_back(object);
        view.resize(viewportSize);
        view.setScene(&scene);
        view.setCameraPose(closeCamera.pose);
        view.setSelected(0);
        view.show();
        QCoreApplication::processEvents();
        int starts=0,finishes=0,deltas=0;
        float absoluteDrag=0.f;
        QObject::connect(&view,&QtEditorViewport::gizmoDragStarted,
                         &view,[&]{++starts;});
        QObject::connect(&view,&QtEditorViewport::gizmoDragFinished,
                         &view,[&]{++finishes;});
        QObject::connect(&view,&QtEditorViewport::gizmoDelta,&view,
                         [&](int,int,float d){++deltas;absoluteDrag+=std::abs(d);});
        for(int mode=0;mode<3;++mode) {
            view.setGizmoMode(mode);
            auto shape=view.gizmoGeometry();
            auto const pointer=mode==1?shape.rings[2][8]:
                shape.center+(shape.tips[0]-shape.center)*
                    (mode==0?.65:1.);
            auto const hit=pickGizmo(shape,pointer,mode);
            require(hit.axis==(mode==1?2:0),"Test hit location ambiguous");
            auto hover=mouseEvent(QEvent::MouseMove,pointer,Qt::NoButton,
                                  Qt::NoButton,&view);
            QCoreApplication::sendEvent(&view,&hover);
            require(view.hoveredGizmoAxis()==hit.axis,
                    "Gizmo hover highlighting not activated");
            auto press=mouseEvent(QEvent::MouseButtonPress,pointer,
                                  Qt::LeftButton,Qt::LeftButton,&view);
            QCoreApplication::sendEvent(&view,&press);
            QPointF const target=pointer+hit.tangent*14.;
            auto move=mouseEvent(QEvent::MouseMove,target,Qt::NoButton,
                                 Qt::LeftButton,&view);
            QCoreApplication::sendEvent(&view,&move);
            auto release=mouseEvent(QEvent::MouseButtonRelease,target,
                                    Qt::LeftButton,Qt::NoButton,&view);
            QCoreApplication::sendEvent(&view,&release);
            require(starts==mode+1 && finishes==mode+1,
                    "Gizmo mouse press/release not emitted exactly once");
            require(deltas>=mode+1 && absoluteDrag>.001f,
                    "Gizmo drag pointer movement not processed");
        }
        // Pixel-level authoring-grid verification: the overlay must paint
        // visibly across the XZ plane, clear cleanly when disabled, and
        // remain visible after traveling far outside the old 100m limits.
        scene.objects.clear();
        view.setScene(&scene);
        view.setSelected(-1);
        auto const checkGridPixels=[&](CameraPose pose) {
            view.setCameraPose(pose);
            view.setShowGrid(false);
            QCoreApplication::processEvents();
            auto const off=view.grab().toImage().convertToFormat(
                QImage::Format_RGB32);
            view.setShowGrid(true);
            QCoreApplication::processEvents();
            auto const on=view.grab().toImage().convertToFormat(
                QImage::Format_RGB32);
            require(on.size()==off.size(),"Grid frames have different size");
            std::size_t changed=0;
            for(int y=100;y<on.height()-35;y+=2)
                for(int x=40;x<on.width()-40;x+=2) {
                    auto const a=on.pixelColor(x,y);
                    auto const b=off.pixelColor(x,y);
                    if(std::abs(a.red()-b.red())+
                       std::abs(a.green()-b.green())+
                       std::abs(a.blue()-b.blue())>30)++changed;
                }
            require(changed>125,"Qt Viewport grid not painted across floor");
        };
        auto gridPose=closeCamera.pose;
        gridPose.position={12.f,24.f,-40.f};
        gridPose.pitch=-.65f;
        checkGridPixels(gridPose);
        gridPose.position={85340.f,120.f,-49000.f};
        checkGridPixels(gridPose);
        view.close();
        std::cout<<"Qt Move arrows + Rotate rings + Scale squares + infinite floor grid pixels: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Qt Gizmo acceptance failed: "<<e.what()<<"\n";
        return 1;
    }
}
