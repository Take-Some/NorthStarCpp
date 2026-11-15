// NorthStarCpp — native camera system (independent implementation).
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/scene.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace northstarcpp {
class AssetStore;
class CameraTrack;
class PhysicsScene;

enum class CameraMode : std::uint8_t {
    first_person, third_person, vehicle_chase, orbit, free, cinematic
};

struct CameraPose {
    Vector3 position{0.f, 0.f, 0.f};
    float yaw = 0.f;                 // radians; yaw=0 faces +Z
    float pitch = 0.f;               // radians; positive looks up
    float roll = 0.f;                // radians; visual-only, no aim correction
    float fov_y_degrees = 75.f;
    float near_plane = 0.08f;
    float far_plane = 500.f;
};

struct CameraSettings {
    float third_person_distance = 4.f;
    float third_person_height = 0.7f;
    float shoulder_offset = 0.35f;
    float vehicle_distance = 7.f;
    float vehicle_height = 2.4f;
    float vehicle_lookahead_seconds = 0.18f;
    float vehicle_distance_per_speed = 0.075f;
    float vehicle_speed_fov = 0.23f; // extra FOV degrees per unit/s
    float vehicle_reverse_distance = 3.0f;
    float vehicle_roll_degrees = 7.f; // max bank in a steering manoeuvre
    float orbit_distance = 5.f;
    float orbit_speed = 0.5f;       // radians/s
    float follow_smoothing = 0.13f; // seconds, zero means direct tracking
    float collision_radius = 0.22f; // metres
    float collision_padding = 0.08f;
    float teleport_cut_distance = 24.f;
    float default_fov_y = 75.f;    // degrees
    float aimed_fov_y = 55.f;
    float near_plane = 0.08f;
    float far_plane = 500.f;

    // Frame-rate-independent follow response. A zero spring frequency keeps
    // the established exponential follow behavior for existing projects.
    float follow_spring_frequency_hz = 0.f;
    // Predictive framing, filtered independently of the camera pose to
    // avoid camera snaps when vehicle / character speed changes.
    float third_person_lookahead_seconds = 0.12f;
    float third_person_lookahead_max = 1.5f;
    float lookahead_damping_seconds = 0.12f;
    // Camera pulls toward the target immediately when obstructed, but
    // restores its orbit distance smoothly once clearance returns.
    float collision_restore_seconds = 0.10f;
};

// Engine animation graph can supply a world-space camera/head socket pose
// each frame; the camera remains independent of the skeleton implementation.
struct CameraAnimationAttachment {
    bool enabled = false;
    Vector3 world_position{0.f,0.f,0.f};
    float yaw=0.f, pitch=0.f, roll=0.f;
    float weight=1.f;  // blend animated socket with gameplay eye [0,1]
};

struct CameraTarget {
    CameraAnimationAttachment attachment{};
    Vector3 eye{0.f, 0.f, 0.f};   // target eye point, not feet
    Vector3 velocity{0.f, 0.f, 0.f};
    float yaw = 0.f;
    float pitch = 0.f;
    float heading = 0.f; // vehicle facing, independent of orbit input
    float steering = 0.f; // vehicle steering input [-1,1]
    bool reversing = false;
    std::size_t ignore_object = std::numeric_limits<std::size_t>::max();
};

// One fixed world-space cinematic camera request. Priority arbitration is
// engine-owned; gameplay cameras remain active when the stack is empty.
struct CameraShot {
    std::string source;  // unique validated source/preset id
    CameraPose pose{};
    int priority=0;      // [-100,100], higher number wins
    float blend_seconds=.35f;
};

void validate_camera_shot(CameraShot const& shot);
// Game/Editor consume the same bounded JSON preset contract through VFS.
std::vector<CameraShot> parse_camera_presets(std::string const& json);
std::string serialize_camera_presets(std::vector<CameraShot> const& shots);
std::vector<CameraShot> load_camera_presets(AssetStore const& assets,
    std::string const& virtual_path="gameplay/camera_presets.json");

// Optional project tuning; configuration bytes must come from the active VFS
// provider, not a direct filesystem read in camera gameplay.
CameraSettings parse_camera_settings(std::string const& json);
CameraSettings load_camera_settings(AssetStore const& assets,
                                    std::string const& virtual_path = "gameplay/camera.json");

// One engine-owned camera controller, usable by the editor and games; neither
// scene simulation nor the render plugin owns camera gameplay semantics.
class CameraDirector {
public:
    explicit CameraDirector(CameraSettings settings = {});
    ~CameraDirector();
    CameraDirector(CameraDirector const&) = delete;
    CameraDirector& operator=(CameraDirector const&) = delete;
    CameraDirector(CameraDirector&&) noexcept;
    CameraDirector& operator=(CameraDirector&&) noexcept;
    void configure(CameraSettings settings);
    CameraSettings const& settings() const noexcept { return settings_; }
    void set_mode(CameraMode mode, float transition_seconds = 0.35f);
    CameraMode mode() const noexcept { return mode_; }
    void set_zoom(float amount); // 0=base FOV; 1=aimed FOV
    void set_free_pose(CameraPose pose);
    void set_cinematic_pose(CameraPose pose);
    void set_track(CameraTrack track, bool loop = false);
    void clear_track() noexcept;
    void seek_track(float seconds);
    void set_track_playing(bool playing) noexcept { track_playing_ = playing; }
    void set_track_playback_rate(float rate);
    float track_playback_rate() const noexcept { return track_rate_; }
    bool has_track() const noexcept { return bool(track_); }
    float track_time() const noexcept { return track_time_; }
    void shake(float amplitude_metres, float duration_seconds);
    // Arbitration: higher priority wins; most recently updated wins ties.
    // A source is updated in-place, never duplicated. At most 16 active
    // requests, regardless of which JS/plugin subsystem owns the source.
    void push_shot(CameraShot shot);
    void clear_shot(std::string const& source);
    void clear_shots() noexcept;
    std::string const& active_shot() const noexcept {return active_shot_;}
    std::size_t shot_count() const noexcept {return shots_.size();}
    // Immediate view cut (teleport/shot) without rewinding a cinematic track.
    void cut() noexcept;
    void reset();
    CameraPose update(float dt, CameraTarget const& target, Scene const* world = nullptr,
                      PhysicsScene const* physics = nullptr);
    CameraPose const& pose() const noexcept { return pose_; }

private:
    CameraPose desired(CameraTarget const& target, float dt);
    Vector3 clip_line_of_sight(Vector3 pivot, Vector3 wanted,
                              Scene const& world, std::size_t ignore) const;

    CameraSettings settings_;
    CameraMode mode_ = CameraMode::first_person;
    CameraPose pose_{};
    CameraPose transition_from_{};
    CameraPose free_pose_{};
    CameraPose cinematic_pose_{};
    std::unique_ptr<CameraTrack> track_;
    float track_time_ = 0.f;
    bool track_loop_ = false;
    bool track_playing_ = true;
    float track_rate_ = 1.f;
    Vector3 previous_eye_{0.f, 0.f, 0.f};
    Vector3 follow_velocity_{0.f, 0.f, 0.f};
    Vector3 lookahead_offset_{0.f, 0.f, 0.f};
    float collision_limit_ = -1.f;
    bool initialized_ = false;
    float transition_seconds_ = 0.f;
    float transition_elapsed_ = 0.f;
    float zoom_ = 0.f;
    float orbit_phase_ = 0.f;
    float time_ = 0.f;
    float shake_remaining_ = 0.f;
    float shake_duration_ = 0.f;
    float shake_amplitude_ = 0.f;
    struct SequencedShot {
        CameraShot shot;
        std::uint64_t order=0;
    };
    std::vector<SequencedShot> shots_;
    std::string active_shot_;
    std::uint64_t shot_order_=0;
    float last_shot_blend_seconds_=0.f;
};

} // namespace northstarcpp
