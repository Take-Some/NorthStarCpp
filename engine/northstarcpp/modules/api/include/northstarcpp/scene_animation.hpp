// NorthStarCpp — provider-neutral scene transform animation
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#pragma once

#include <northstarcpp/scene.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace northstarcpp {

class AssetStore;

// Authored offsets relative to an object's transform at playback start.
// Scale is a positive multiplier; rotation is an additive Euler offset (degrees).
struct SceneAnimationPose {
    Vector3 translation{0.f, 0.f, 0.f};
    Vector3 rotation{0.f, 0.f, 0.f};
    Vector3 scale{1.f, 1.f, 1.f};
};

struct SceneAnimationKeyframe {
    float time = 0.f;
    SceneAnimationPose pose{};
};

enum class SceneAnimationInterpolation { linear, step, fermi_dirac };

class SceneAnimationClip {
public:
    void set_keyframes(std::vector<SceneAnimationKeyframe> frames,
                       SceneAnimationInterpolation interpolation = SceneAnimationInterpolation::linear);
    SceneAnimationPose sample(float seconds) const;
    float duration() const noexcept;
    std::size_t keyframe_count() const noexcept { return frames_.size(); }
    SceneAnimationInterpolation interpolation() const noexcept { return interpolation_; }

private:
    std::vector<SceneAnimationKeyframe> frames_;
    SceneAnimationInterpolation interpolation_ = SceneAnimationInterpolation::linear;
};

// JSON format schema=1, with 2..8192 keyframes and a bounded source size.
SceneAnimationClip parse_scene_animation_clip(std::string const& json);
SceneAnimationClip load_scene_animation_clip(AssetStore const& assets, std::string const& virtual_path);

struct SceneAnimationSample {
    std::uint64_t entity_id = 0;
    std::size_t scene_index = 0;
    Vector3 position{};
    Vector3 rotation{};
    Vector3 scale{1.f, 1.f, 1.f};
};

struct SceneAnimationStatus {
    float time = 0.f;
    float duration = 0.f;
    float speed = 1.f;
    bool loop = false;
    bool paused = false;
    bool finished = false;
};

// Scene-specific playback state. Does not own the native physics or renderer.
class SceneAnimationPlayer {
public:
    void play(Scene const& scene, std::uint64_t entity_id,
              SceneAnimationClip clip, bool loop = false, float speed = 1.f);
    void stop(std::uint64_t entity_id) noexcept;
    void clear() noexcept;
    bool active(std::uint64_t entity_id) const noexcept;
    void pause(std::uint64_t entity_id, bool paused);
    void seek(std::uint64_t entity_id, float seconds);
    SceneAnimationStatus status(std::uint64_t entity_id) const;
    std::vector<SceneAnimationSample> advance(float seconds);
    // Zero-allocation steady-state sampling into caller-owned frame storage.
    void advance_into(float seconds,std::vector<SceneAnimationSample>& output);
    std::size_t count() const noexcept { return playing_.size(); }

private:
    struct Instance {
        SceneAnimationClip clip;
        std::size_t scene_index = 0;
        std::uint64_t entity_id = 0;
        Vector3 base_position{};
        Vector3 base_rotation{};
        Vector3 base_scale{1.f, 1.f, 1.f};
        float time = 0.f;
        float speed = 1.f;
        bool loop = false;
        bool paused = false;
        bool finished = false;
    };
    std::unordered_map<std::uint64_t, Instance> playing_;
    // Sorted only when instances change, not once per rendered frame.
    std::vector<std::uint64_t> scene_order_;
};

} // namespace northstarcpp
