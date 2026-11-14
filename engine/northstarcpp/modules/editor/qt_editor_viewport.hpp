// Cross-platform Qt scene authoring viewport; no native window handle.
// CPU wireframe proxy preview, to be replaced with RenderGraph-backed surface.
#pragma once
#include "editor_camera.hpp"
#include <northstarcpp/scene.hpp>
#include <QPoint>
#include <QVector3D>
#include <QWidget>
class QDragEnterEvent;
class QDropEvent;
class QMouseEvent;
class QWheelEvent;
namespace northstarcpp::editor {
class QtEditorViewport final : public QWidget {
    Q_OBJECT
public:
    explicit QtEditorViewport(QWidget* parent=nullptr);
    void setScene(Scene const* value);
    void setSelected(int index);
    void setGizmoMode(int index);
    int selected() const noexcept {return selected_;}
    void frameSelected();
signals:
    void selectionChanged(int index);
    void assetDropped(QString path, QVector3D position);
    void gizmoDelta(int mode,int axis,float delta);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
private:
    Scene const* scene_{};
    editor_tools::EditorCamera camera_;
    int selected_=-1, gizmo_=0, drag_axis_=-1;
    bool rotating_=false;
    QPoint previous_;
    QPointF projected(Vector3 p) const;
    Vector3 groundAt(QPoint pos) const;
};
}
