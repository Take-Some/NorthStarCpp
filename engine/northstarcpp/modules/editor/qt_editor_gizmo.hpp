// NorthStarCpp Qt Editor transform gizmo geometry, rendering and picking.
// Screen-space handles share the same camera and transform axes as the GPU
// renderer, with stable apparent size and no dependency on a render provider.
#pragma once
#include "editor_camera.hpp"
#include <northstarcpp/scene.hpp>
#include <QPointF>
#include <QSize>
#include <QVector>
#include <array>
class QPainter;

namespace northstarcpp::editor {
struct GizmoHit {
    int axis=-1;
    QPointF tangent{};
    float pixel_length=0.f;
};
struct GizmoScreenGeometry {
    QPointF center{};
    std::array<QPointF,3> tips{};
    std::array<QVector<QPointF>,3> rings{};
    std::array<bool,3> axis_visible{};
    float world_length=0.f;
    bool visible=false;
};
// Scale is computed using camera projection so the handle remains
// approximately 90 px long across arbitrary scene zoom / object sizes.
GizmoScreenGeometry makeGizmoGeometry(
    editor_tools::EditorCamera const& camera,
    SceneObject const& object,QSize viewport,int mode,bool local=false);
GizmoHit pickGizmo(GizmoScreenGeometry const& geometry,
                   QPointF pointer,int mode,float threshold=12.f);
void paintGizmo(QPainter& painter,GizmoScreenGeometry const& geometry,
                int mode,int selected_axis,int hovered_axis);
float gizmoDragAmount(GizmoHit const& handle,QPointF screen_delta,
                      float world_length,int mode);
} // namespace northstarcpp::editor
