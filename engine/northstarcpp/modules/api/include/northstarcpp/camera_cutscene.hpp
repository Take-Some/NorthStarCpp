// NorthStar native cutscene shot timeline. Independently implemented.
#pragma once
#include <northstarcpp/camera_track.hpp>
#include <cstddef>
#include <string>
#include <vector>

namespace northstarcpp {
class AssetStore;

// Shots may use authored world coordinates or a pose relative to the
// controlled character/vehicle at the moment the cutscene begins.
enum class CameraCutsceneSpace { world, target };

// A shot has a local camera animation. Playback advances on the director's
// clock; transition_seconds cross-fades from the previous shot's final pose.
struct CameraCutsceneShot {
    std::string name;
    float duration=0.f;
    float transition_seconds=0.f;
    CameraTrack track;
};
struct CameraCutsceneFrame {
    CameraPose pose;
    std::string shot;
    std::size_t index=0;
    float local_seconds=0.f;
};
// All cue timestamps use the global sequence timeline, not local shot time.
// Scene animation/audio/dialogue remain script-owned: the director emits
// deterministic commands without hardcoding project-specific behavior.
struct CameraCutsceneCue {
    float seconds=0.f;
    std::string lane;
    std::string type; // animation, audio, dialogue, event, overlay
    std::string target;
    std::string action;
    std::string value;
};
struct CameraCinematicLens {
    float focus_metres=5.f;
    float aperture_f=4.f;
    float exposure_ev=0.f;
    float motion_blur=0.f;
    float letterbox=0.f;
    float dof_strength=0.f;
};
struct CameraLensKey {
    float seconds=0.f;
    CameraCinematicLens lens;
};

class CameraCutscene {
public:
    void set_shots(std::vector<CameraCutsceneShot> shots);
    bool empty() const noexcept { return shots_.empty(); }
    std::size_t size() const noexcept { return shots_.size(); }
    float duration() const noexcept { return duration_; }
    CameraCutsceneSpace space() const noexcept { return space_; }
    void set_space(CameraCutsceneSpace value) noexcept { space_=value; }
    CameraCutsceneFrame sample(float time) const;
    void set_cues(std::vector<CameraCutsceneCue> cues);
    void set_lens_keys(std::vector<CameraLensKey> keys);
    std::vector<CameraCutsceneCue> cues_between(float from_exclusive,
                                                 float to_inclusive) const;
    CameraCinematicLens lens_at(float seconds) const;
    std::vector<CameraCutsceneCue> const& cues() const noexcept { return cues_; }
    std::vector<CameraLensKey> const& lens_keys() const noexcept { return lens_keys_; }
    std::vector<CameraCutsceneShot> const& shots() const noexcept { return shots_; }
private:
    std::vector<CameraCutsceneShot> shots_;
    std::vector<float> starts_;
    float duration_=0.f;
    CameraCutsceneSpace space_=CameraCutsceneSpace::world;
    std::vector<CameraCutsceneCue> cues_;
    std::vector<CameraLensKey> lens_keys_;
};

// JSON v1: {"schema":1,"shots":[{"name":"...",
// "duration":2,"transition_seconds":0.4,"track":{"schema":1,
// "keyframes":[{"time":0,...}]}}]}. Tracks reuse the native rail codec.
// All external files must be read through the active VFS.
CameraCutscene parse_camera_cutscene(std::string const& json);
std::string serialize_camera_cutscene(CameraCutscene const& scene);
std::string serialize_camera_cues(std::vector<CameraCutsceneCue> const& cues);
CameraCutscene load_camera_cutscene(AssetStore const& assets,
    std::string const& virtual_path);
} // namespace northstarcpp
