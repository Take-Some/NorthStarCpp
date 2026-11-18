#pragma once
// App-only state: order of members is preserved to keep resource lifetime rules.
// Included from window_app_internal.hpp inside northstarcpp::window_app_internal.
struct AppRuntimeState {
    using ScreenPoint = editor_tools::ScreenPoint;
    explicit AppRuntimeState(Options settings, diagnostics::StartupReport* startup_report)
        : config(std::move(settings)), startup_report_(startup_report),
          runtime_services_(config,file_path) {}
    Options config;
    diagnostics::StartupReport* startup_report_=nullptr;
    bool runtime_faulted = false;
    mutable bool error_dialog_active = false;
    std::unique_ptr<diagnostics::FrameProfiler> frame_profiler;
    std::unique_ptr<ScriptRuntime> script_runtime;
    // Reuse bounded UI JSON output storage across frame ticks.
    std::string ui_event_payload_scratch;
    std::unique_ptr<SceneAnimationPlayer> script_animations;
    std::unique_ptr<VehicleService> script_vehicle;
    // Dedicated render-only scratch. Reuse Scene and vehicle enumeration
    // capacities across frames; never mutate the authored PhysicsScene.
    Scene vehicle_visual_scene_scratch;
    std::vector<NorthStarVehicleState> vehicle_before_scratch;
    std::vector<NorthStarVehicleState> vehicle_after_scratch;
    std::vector<NorthStarVehicleState> vehicle_draw_states_scratch;
    // Preserve parent transform memo allocations across frames.
    std::vector<std::pair<std::uint32_t,std::array<float,6>>>
        vehicle_parent_angles_scratch;
    std::unordered_map<std::uint64_t,std::pair<std::uint32_t,std::uint32_t>> script_vehicle_occupant_seats;
    std::unordered_map<std::uint32_t,VehicleRigAsset> script_vehicle_rigs;
    std::unordered_map<std::uint32_t,std::vector<std::uint32_t>> script_vehicle_visual_indices;
    std::unordered_map<std::uint32_t,std::vector<std::uint32_t>> script_vehicle_door_visual_indices;
    struct VehicleVisualFollower {
        std::uint32_t child_index=0, parent_index=0;
        Vector3 local_offset{}, local_rotation{};
    };
    // Resolved once per loaded rig, then updated in the existing vehicle
    // render pass. A composite mesh never creates extra Jolt rigid bodies.
    std::unordered_map<std::uint32_t,std::vector<VehicleVisualFollower>>
        script_vehicle_visual_followers;
    struct VehicleTarget {
        float x,z,speed,radius;
    };
    std::unordered_map<std::uint32_t,VehicleTarget> script_vehicle_targets;
    std::unordered_set<std::uint32_t> script_vehicle_native;
    std::vector<SceneAnimationSample> script_animation_samples;
    std::unordered_map<std::size_t,SceneAnimationSample> script_animation_visuals;
    std::unique_ptr<AssetStore> script_assets;
    std::unique_ptr<EcsWorld> script_ecs;
    std::unordered_map<std::uint64_t,std::size_t> script_entity_indices;
    std::unordered_map<NsEntity,std::uint64_t> script_handle_ids;
    std::unordered_map<std::uint64_t,NsEntity> script_spawn_handles;
    std::unordered_map<std::uint64_t,std::uint64_t> script_spawn_owners;
    std::uint64_t next_script_spawn_id=0;
    std::uint64_t script_context_epoch=0;
    std::uint64_t active_script_context_epoch=0;
    // Each ScriptRuntime receives an independent lease. During failed hot
    // reload the old context remains valid while the candidate is revoked.
    std::unordered_map<std::uint64_t,std::shared_ptr<ScriptBindingLease>>
        script_binding_leases;
    // Incremented only when a whole scene is replaced or torn down.
    std::uint64_t script_scene_generation=1;
    fs::path script_source_root;
    fs::path requested_project_scene_; // deferred generic scene transition
    std::map<fs::path,fs::file_time_type> script_file_stamps;
    Clock::time_point script_probe_last{};
    bool script_entry_exists=false;
    bool script_suspended=false;
    std::wstring script_window_title;
    UiDocument script_ui_document_{};
    std::unordered_map<std::string,UiDocument> script_world_ui_documents_;
    bool scripted_game=false;
    bool scripted_first_person=false;
    bool script_paused=false;
    // Editor-controlled simulation pause and possession are independent of
    // game-authored script pause. Neither survives Player termination.
    runtime_services::AppSimulationState runtime_state_;
    player_action_events::Tracker player_action_tracker_;
    float script_move_x=0.f, script_move_z=0.f;
    float script_yaw=0.f, script_pitch=0.f, script_eye_height=1.45f;
    std::optional<NorthStarRenderPostFxSettings> cinematic_postfx_original_;
    float cinematic_postfx_applied_ev_=1000.f;
    bool cinematic_postfx_unavailable_=false;
    float script_mouse_dx=0.f, script_mouse_dy=0.f;
    std::int32_t script_wheel_vertical=0;
    std::array<std::uint8_t,5> script_mouse_buttons{};
    std::array<NorthStarInputGamepad,NORTHSTARCPP_INPUT_MAX_GAMEPADS> script_gamepads{};
    std::unordered_map<std::size_t,std::uint32_t> script_object_colors;
    // Ephemeral per-entity YCD selection. The authored scene is unchanged.
    std::unordered_map<std::uint64_t,std::string> script_model_animations;
    // Queue animation commands from scripts started before viewport init.
    std::unordered_map<std::uint64_t,float> script_model_animation_rates;
    std::unordered_map<std::uint64_t,std::string> script_model_visual_modes;
    std::unordered_map<std::uint64_t,std::string> script_model_bone_attachments;
    std::unordered_map<std::string,FpsAudioClip> script_audio_cache;
    std::unordered_map<std::string,std::vector<nac::Track>> script_nac_cache;
    std::unique_ptr<AudioEngine> script_voice_audio;
    bool script_logging_allowed = true;
    std::unique_ptr<Logger> script_logger;
    std::unique_ptr<PerlinNoise> script_noise;
    struct NpcRuntime {
        gameplay::AIController controller;
        std::optional<player_animation::Controller> animation;
        std::uint64_t mesh_id=0;
        Vector3 previous_position{0.f,0.f,0.f};
        bool have_previous=false;
        bool motor_active=false;
        float visual_yaw=0.f;
        bool have_visual_yaw=false;
        std::optional<navigation::RuntimeNavMesh> navmesh;
        std::vector<Vector3> path;
        std::size_t path_cursor=0;
        float stall_seconds=0.f;
        float replanning_delay=0.f;
        bool route_partial=false;
        bool route_failed=false;
    };
    std::unordered_map<std::uint64_t,NpcRuntime> script_ai_controllers;
    std::vector<std::pair<std::uint64_t,gameplay::PossessionEvent>> npc_controller_event_scratch;
    std::uint64_t reported_controller_event_overruns_=0;
    std::array<std::uint8_t,NORTHSTARCPP_INPUT_MAX_KEYS> script_keys{};
    gameplay::InputActionMap script_input_actions_{};
    HWND window = nullptr, surface = nullptr, list = nullptr;
    HWND player = nullptr, collision_flag = nullptr, inspector_tabs = nullptr;
    HACCEL accelerators = nullptr;
    HWND apply = nullptr, status = nullptr;
    HWND outliner_title=nullptr, outliner_search=nullptr, details_title=nullptr;
    HWND viewport_title=nullptr, content_title=nullptr;
    HWND local_button=nullptr, snap_button=nullptr, step_edit=nullptr;
    HWND bind_button=nullptr, place_button=nullptr, reveal_button=nullptr;
    std::array<HWND,3> gizmo_buttons{};
    std::unique_ptr<EditorContentBrowser> content_browser;
    std::vector<std::size_t> outliner_indices;
    editor_tools::GizmoMode gizmo_mode=editor_tools::GizmoMode::translate;
    editor_tools::GizmoOptions gizmo_settings{};
    bool content_visible=false;
    int left_width=240,right_width=295,content_height=250;
    enum class Splitter {none,left,right,bottom};
    Splitter splitter_drag=Splitter::none;
    editor_tools::EditorCamera editor_camera{};
    bool camera_looking=false;
    ScreenPoint camera_pointer{};
    float camera_move_speed=8.f;
    bool gizmo_dragging=false;
    int gizmo_drag_axis=-1;
    float gizmo_drag_amount=0.f;
    ScreenPoint gizmo_start{};
    Scene gizmo_baseline;
    std::optional<Scene> gizmo_scene;
    int inspector_page = 0;
    editor_tools::History history;
    std::array<HWND, 8> buttons{};
    std::array<HWND, 6> labels{}, edits{};
    std::unique_ptr<Viewport> graphics;
    std::string graphics_detail_level="cinematic";
    SkyEnvironment sky_environment_;
    // Explicit scene opt-out takes precedence over script weather settings.
    std::string sky_mode_="disabled";
    bool sky_scene_disabled_=false;
    std::string sky_drawable_reference_;
    bool sky_visual_dirty_=false;
    double sky_visual_elapsed_=0.;
    EcsWorld document;
    window_detail::WindowSceneStreaming scene_streaming;
    bool show_streaming_overlay=false;
    std::unique_ptr<PhysicsScene> preview;
    std::unique_ptr<TerrainPhysicsBridge> terrain_physics_bridge;
    std::unique_ptr<world::TerrainAsyncStreamer> terrain_async_streamer;
    std::unique_ptr<world::TerrainProvider> terrain_contact_provider;
    std::unique_ptr<world::TerrainStreamer> terrain_editor_streamer;
    PhysicsScene* terrain_attached_preview=nullptr;
    std::map<world::ChunkKey,world::TerrainChunk> terrain_visual_chunks;
    world::TerrainConfig terrain_settings{};
    world::Vec3d terrain_last_focus{};
    std::string terrain_follow_entity,terrain_material;
    fs::path terrain_config_path;
    std::size_t terrain_follow_index=std::numeric_limits<std::size_t>::max();
    bool terrain_config_checked=false,terrain_config_enabled=false;
    bool terrain_have_focus=false;
    std::unique_ptr<AudioOutput> audio;
    std::unique_ptr<InputDevice> input;
    std::unique_ptr<FpsArena> fps;
    std::unique_ptr<FpsWeaponEffects> fps_weapon_fx;
    std::unique_ptr<FpsWeaponEffects> script_weapon_fx;
    std::unique_ptr<AssetManager> fps_assets;
    std::unique_ptr<AssetManager> zone_assets;
    std::unique_ptr<AssetZones> asset_zones;
    FpsTuning fps_tuning;
    AssetRequest<FpsAudioClip> fps_shot, fps_reload;
    CameraSettings fps_camera_settings{};
    struct CharacterPoseRequest {
        std::size_t scene_index=0;
        float heading=0.f;
        std::string animation;
        bool first_person_arms=false;
    };
    std::optional<CharacterAssetSpec> script_character;
    std::optional<CharacterPoseRequest> script_character_pose;
    bool script_character_dirty=false;
    bool script_character_renderable=false;
    std::unique_ptr<CameraDirector> fps_camera;
    CameraPose fps_pose{};
    // Gameplay Camera View Target lifecycle, independent from Jolt possession.
    std::uint64_t camera_view_target_last=0;
    bool camera_view_target_initialized=false;
    float camera_view_target_blend_remaining=0.f;
    std::unique_ptr<AssetStore> fps_ui_assets_;
    UiDocument fps_ui_document_{};
    fs::path fps_ui_source_{};
    fs::file_time_type fps_ui_stamp_{};
    Clock::time_point fps_ui_probe_last_{};
    bool ui_mouse_down = false;
    bool fps_captured = false, fps_menu_visible = false;
    bool fps_previous_menu = false, fps_previous_jump = false,
         fps_previous_reload = false;
    bool explore_previous_use = false;
    fs::path file_path;
    // Constructed after the two referenced App members; destroyed first.
    runtime_services::RuntimeServices runtime_services_;
    NsEntity selected = 0;
    int exit_status = 0;
    bool dirty = false;
    timing::FrameClock frame_clock = [] {
        timing::FrameClock clock;
        clock.start();
        return clock;
    }();
};
