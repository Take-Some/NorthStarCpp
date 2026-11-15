// Native engine.render is the default Windows authoring view.
// Qt CPU proxies remain explicit diagnostics or a provider fallback.
#pragma once
#include "editor_camera.hpp"
#include "qt_editor_gizmo.hpp"
#include <northstarcpp/scene.hpp>
#include <QPoint>
#include <northstarcpp/frame_clock.hpp>
#include <QSet>
#include <QString>
#include <QVector3D>
#include <QWidget>
#include <filesystem>
#include <memory>
class QDragEnterEvent;
class QDropEvent;
class QDragMoveEvent;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QFocusEvent;
class QTimer;
class QEvent;
class QResizeEvent;
namespace northstarcpp::editor {
class QtEditorViewport final : public QWidget {
    Q_OBJECT
public:
    explicit QtEditorViewport(QWidget* parent=nullptr);
    ~QtEditorViewport() override;
    void setAssetSources(std::filesystem::path projectRoot);
    bool gpuActive() const noexcept;
    QString gpuError() const {return gpu_error_;}
    bool renderGpuNow();
    // Returns last native GPU frame's host-side submission summary.
    QString gpuAssetSummary() const;
    void setScene(Scene const* value);
    void setSelected(int index);
    void setGizmoMode(int index);
    void setGizmoLocal(bool enabled);
    int hoveredGizmoAxis() const noexcept {return hovered_axis_;}
    GizmoScreenGeometry gizmoGeometry() const;
    int selected() const noexcept {return selected_;}
    void frameSelected();
    void frameAll();
    // EngineGpu uses the same YDD/YDR+YMAT/YTD material path as Player.
    // Shaded/Wireframe remain explicit low-cost proxy diagnostics.
    enum class DrawMode:int {
        Shaded=0, Wireframe=1, ShadedWireframe=2, EngineGpu=3
    };
    void setDrawMode(DrawMode mode);
    DrawMode drawMode() const noexcept {return draw_mode_;}
    void setShowGrid(bool enabled);
    bool showGrid() const noexcept {return show_grid_;}
    void setFlySpeed(float metersPerSecond);
    float flySpeed() const noexcept {return fly_speed_;}
    editor_tools::EditorCamera const& camera() const noexcept {return camera_;}
    void setCameraPose(CameraPose pose);
    // Preview Studio orbit; opt-in and isolated from the scene fly-camera.
    void setOrbitMode(bool enabled,Vector3 target={0.f,1.f,0.f},
                      float distance=4.2f);
    void setOrbitAngles(float yaw,float pitch);
    bool orbitMode() const noexcept {return orbit_mode_;}
    float orbitDistance() const noexcept {return orbit_distance_;}
    void advanceFly(float elapsedSeconds);
signals:
    void flySpeedChanged(float metersPerSecond);
    void selectionChanged(int index);
    void assetDropped(QString path, QVector3D position,int targetActor);
    void gizmoDelta(int mode,int axis,float delta);
    void gizmoModeRequested(int mode);
    void gizmoDragStarted();
    void gizmoDragFinished();
    void gpuUnavailable(QString error);
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool event(QEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
private:
    Scene const* scene_{};
    editor_tools::EditorCamera camera_;
    int selected_=-1, gizmo_=0, drag_axis_=-1, hovered_axis_=-1;
    bool gizmo_local_=false;
    GizmoHit drag_handle_{};
    float drag_world_length_=0.f;
    bool rotating_=false;
    bool orbit_mode_=false;
    float orbit_distance_=4.2f;
    Vector3 orbit_target_{0.f,1.f,0.f};
    bool panning_=false;
    bool show_grid_=true;
    DrawMode draw_mode_=DrawMode::Shaded; // activated after Qt window show
    float fly_speed_=12.f;
    QSet<int> held_keys_;
    northstarcpp::timing::FrameClock fly_clock_;
    QTimer* fly_timer_{};
    QTimer* gpu_timer_{};
    QWidget* gpu_surface_{};
    struct GpuState;
    std::unique_ptr<GpuState> gpu_;
    std::filesystem::path asset_root_;
    bool gpu_dirty_=true;
    QString gpu_error_;
    void updateOrbitCamera();
    void ensureGpu();
    void drawGpu();
    QPoint previous_;
    QPointF projected(Vector3 p) const;
    Vector3 groundAt(QPoint pos) const;
};
}
