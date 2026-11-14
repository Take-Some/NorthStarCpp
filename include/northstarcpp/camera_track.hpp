// NorthStarCpp — cinematic camera tracks, independent from animation backends.
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/camera.hpp>
#include <string>
#include <vector>

namespace northstarcpp {
struct CameraKeyframe {
    float seconds = 0.f;
    CameraPose pose{};
};

class CameraTrack {
public:
    void set_keyframes(std::vector<CameraKeyframe> keyframes);
    bool empty() const noexcept { return frames_.empty(); }
    float duration() const noexcept;
    std::size_t size() const noexcept { return frames_.size(); }
    std::vector<CameraKeyframe> const& keyframes() const noexcept { return frames_; }
    CameraPose sample(float seconds) const;

private:
    std::vector<CameraKeyframe> frames_;
};

// Serializable camera rail, read only through the current asset provider.
CameraTrack parse_camera_track(std::string const& json);
std::string serialize_camera_track(CameraTrack const& track);
CameraTrack load_camera_track(AssetStore const& assets,
                              std::string const& virtual_path = "cinematics/intro.camera.json");
// In-memory camera take recorder. A finished take can be serialized to a
// replay asset with serialize_camera_track() and played by CameraDirector.
// Recording is explicit; no camera/gameplay state is written automatically.
class CameraReplayRecorder {
public:
    void start(float sample_interval_seconds = 1.f/30.f);
    void record(float dt, CameraPose const& pose);
    void pause(bool paused) noexcept { paused_=paused; }
    bool recording() const noexcept { return active_; }
    std::size_t samples() const noexcept { return frames_.size(); }
    float elapsed() const noexcept { return elapsed_; }
    CameraTrack finish();
private:
    std::vector<CameraKeyframe> frames_;
    float interval_ = 1.f/30.f;
    float elapsed_ = 0.f;
    float last_sample_ = 0.f;
    bool active_ = false;
    bool paused_ = false;
};
} // namespace northstarcpp
