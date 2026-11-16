// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <memory>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <optional>
#include <cstdint>
#include <vector>

#include <northstarcpp/scene.hpp>
#include <northstarcpp/render_postfx_api.h>
#include <northstarcpp/render_timing_api.h>
#include "ui_document.hpp"

namespace northstarcpp {

class PhysicsScene;
class SceneWorld;
class FpsArena;
struct CameraPose;
namespace shared_assets { struct DrawableResources; }
class AssetStore;

// Player-independent skeletal resource description supplied by project scripts.
// Model, rig, materials, textures and animation names are virtual VFS paths.
struct CharacterAnimationAsset {
    std::string name;
    std::string source;
    std::string clip;
};
struct CharacterAssetSpec {
    std::string skeleton;
    std::string model;
    std::string material;
    std::string textures;
    std::vector<CharacterAnimationAsset> animations;
};

// Authoring-only overlay; not part of the engine renderer ABI.
struct ViewportGizmo {
    Vector3 axes[3]{{1.f,0.f,0.f},{0.f,1.f,0.f},{0.f,0.f,1.f}};
    unsigned mode = 0; // 0 translate, 1 rotate, 2 scale
    int active_axis = -1;
    bool show_grid=false;
    bool show_collisions=false;
    int grid_width=1280;
    int grid_height=720;
    float length=2.4f; // constant screen-relative handle length per view
};

// Per-frame CPU submission statistics for Editor diagnostics and regression
// tests. No renderer-provider ABI changes: counts describe the host batches.
struct ViewportAssetStats {
    std::size_t native_meshes = 0;
    std::size_t material_batches = 0;
    std::size_t textured_material_batches = 0;
    std::size_t cube_proxies = 0;
    std::size_t invalid_meshes = 0;
};

class Viewport {
public:
    explicit Viewport(HWND window, bool software);
    ~Viewport();

    // Actual selected GPU provider for startup diagnostics.
    std::string renderer_provider_name() const;
    std::string renderer_provider_version() const;
    ViewportAssetStats asset_stats() const noexcept;
    // GPU-backed cube ID selection when supported by the active provider.
    // nullopt = unsupported/incomplete scene; -1 = valid GPU background hit.
    std::optional<int> pick_actor_gpu(std::uint32_t x,std::uint32_t y) const;
    // Provider-owned bounded GPU readback queue: submit returns immediately,
    // poll never waits on a GPU fence. UNKNOWN/STALE results are discarded.
    enum class PickStatus { Pending, Ready, Stale, Unknown };
    struct PickResult {PickStatus status=PickStatus::Unknown;int actor=-1;};
    bool supports_async_gpu_pick() const noexcept;
    std::optional<std::uint64_t> enqueue_actor_gpu_pick(
        std::uint32_t x,std::uint32_t y) const;
    std::optional<PickResult> poll_actor_gpu_pick(std::uint64_t id) const;

    // Project scripts select every skeletal resource and animation state.
    // The native viewport only decodes VFS assets and evaluates the skin pose.
    void load_character(AssetStore const& assets, CharacterAssetSpec const& spec);
    void pose_character(Vector3 foot, float heading, float dt,
                        std::string const& animation,
                        bool first_person_arms = false,
                        Vector3 eye = {}, float pitch = 0.f);
    // Uses the existing character provider, switching triangle index sets
    // without constructing a second skinned avatar or a fake UI overlay.
    void set_character_viewmodel(bool first_person_arms);
    void set_ui_document(UiDocument document);
    // Controls how far authored mesh detail remains resident/visible.
    // No simplification, decimation or substitution of original triangles.
    void set_detail_level(std::string_view level);
    std::string detail_level() const;
    // UI input is dispatched only on the Player's UI/window thread.
    // Returning true means an interactive node consumed the pointer.
    bool ui_hit_test(float x,float y) const;
    bool ui_pointer(float x,float y,std::uint32_t phase);
    bool ui_key(std::uint32_t key,bool down,std::string_view utf8 = {});
    bool ui_wheel(float delta_x,float delta_y);
    std::vector<UiInteractionEvent> take_ui_events();
    void hide_character();
    void clear_character();
    void set_sky(shared_assets::DrawableResources const& resources);
    // Mount source or archived project assets for authored OBJ scene meshes.
    // Geometry is loaded through AssetStore, never by render-provider filesystem I/O.
    struct ShaderReloadProgress {
        bool pending=false;
        std::vector<std::string> completed;
    };
    // Shader source links are read from native YMAT XML material definitions.
    // Vulkan fragment SPIR-V is supplied via an optional render provider ABI.
    std::size_t queue_fragment_shader_reload(
        std::filesystem::path const& shader_file,
        std::vector<std::uint8_t> const& spirv);
    ShaderReloadProgress poll_shader_reload();
    // Runtime GPU-grade control via the active render provider. No engine
    // globals, no hardcoded HUD treatment, and no provider fallback.
    bool supports_postfx() const noexcept;
    void set_postfx_settings(NorthStarRenderPostFxSettings const& settings);
    std::optional<NorthStarRenderPostFxSettings> postfx_settings() const;
    // Synchronous diagnostic GPU timestamps; read() may wait for the GPU
    // and must not be used in the production hot path.
    std::optional<NorthStarRenderTimingStats> gpu_frame_timing() const;
    void set_asset_sources(std::filesystem::path const& project_root,
                           std::filesystem::path const& archive = {},
                           std::filesystem::path const& archive_data = {},
                           std::filesystem::path const& shared_root = {});

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
