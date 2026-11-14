// Cross-platform Qt scene authoring viewport; no native window handle.
// CPU wireframe proxy preview, to be replaced with RenderGraph-backed surface.
#pragma once
#include "editor_camera.hpp"
#include <northstarcpp/scene.hpp>
#include <QPoint>
#include <northstarcpp/frame_clock.hpp>
#include <QSet>
#include <QVector3D>
#include <QWidget>
#include <filesystem>
#include <memory>
class QDragEnterEvent;
class QDropEvent;
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
    bool renderGpuNow();
    void setScene(Scene const* value);
    void setSelected(int index);
    void setGizmoMode(int index);
    int selected() const noexcept {return selected_;}
    void frameSelected();
    void frameAll();
    // Render modes are authoring proxy modes; Vulkan/PBR rendering is
    // independent and will use a dedicated render-target integration.
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
    void advanceFly(float elapsedSeconds);
signals:
    void flySpeedChanged(float metersPerSecond);
    void selectionChanged(int index);
    void assetDropped(QString path, QVector3D position);
    void gizmoDelta(int mode,int axis,float delta);
    void gizmoModeRequested(int mode);
    void gizmoDragStarted();
    void gizmoDragFinished();
    void gpuUnavailable(QString error);
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
private:
    Scene const* scene_{};
    editor_tools::EditorCamera camera_;
    int selected_=-1, gizmo_=0, drag_axis_=-1;
    bool rotating_=false;
    bool panning_=false;
    bool show_grid_=true;
    DrawMode draw_mode_=DrawMode::Shaded;
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
    void ensureGpu();
    void drawGpu();
    QPoint previous_;
    QPointF projected(Vector3 p) const;
    Vector3 groundAt(QPoint pos) const;
};
}
