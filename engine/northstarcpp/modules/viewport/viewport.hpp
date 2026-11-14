// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <memory>
#include <filesystem>

#include <northstarcpp/scene.hpp>

namespace northstarcpp {

class PhysicsScene;
class SceneWorld;
class FpsArena;
struct CameraPose;
namespace shared_assets { struct DrawableResources; }

// Authoring-only overlay; not part of the engine renderer ABI.
struct ViewportGizmo {
    Vector3 axes[3]{{1.f,0.f,0.f},{0.f,1.f,0.f},{0.f,0.f,1.f}};
    unsigned mode = 0; // 0 translate, 1 rotate, 2 scale
    int active_axis = -1;
    float length=2.4f; // constant screen-relative handle length per view
};

class Viewport {
public:
    explicit Viewport(HWND window, bool software);
    ~Viewport();

    // Resolves authored YDD/ymat/YTD data through AssetManager on the host side.
    void set_sky(shared_assets::DrawableResources const& resources);
    // Mount source or archived project assets for authored OBJ scene meshes.
    // Geometry is loaded through AssetStore, never by render-provider filesystem I/O.
    void set_asset_sources(std::filesystem::path const& project_root,
                           std::filesystem::path const& archive = {},
                           std::filesystem::path const& archive_data = {});

    // The renderer consumes a scene snapshot. Simulation and editor state stay on the host side.
    void draw(Scene const& scene, int selected, bool verify_pixels = false,
        PhysicsScene const* physics = nullptr, FpsArena const* fps = nullptr, bool paused = false,
        CameraPose const* camera_pose = nullptr, SceneWorld const* scene_world = nullptr,
        bool show_fps_overlay = true, bool show_streaming_debug = false,
        ViewportGizmo const* gizmo = nullptr);

private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace northstarcpp
