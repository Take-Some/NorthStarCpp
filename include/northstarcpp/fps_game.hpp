// NorthStarCpp FPS gameplay
// Copyright (c) 2026 Kayla's Systems — Kayla Verner
#pragma once
#include <northstarcpp/scene.hpp>
#include <northstarcpp/fps_assets.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace northstarcpp {

// Arena gameplay remains independent of Bullet, Jolt and the Win32 UI.
// The host applies moving-target transforms via the stable PhysicsScene API.
class FpsArena {
public:
    explicit FpsArena(Scene const& authored, FpsTuning tuning = {}, bool allow_empty_targets = false);
    void look(float mouse_dx, float mouse_dy, float stick_x = 0.f, float stick_y = 0.f,
              float dt = 0.f);
    void advance(float dt, Scene const& physics_scene);
    Vector3 direction() const;
    Vector3 eye(Scene const& scene) const;
    Vector3 movement(float strafe, float forward) const;
    bool fire(Scene const& scene);
    bool begin_reload();
    void reset();
    void tint(Scene& scene) const;
    std::vector<std::size_t> const& moving_indices() const { return moving_indices_; }
    Vector3 moving_position(std::size_t index) const;
    std::size_t targets() const { return total_; }
    std::size_t hits() const { return hits_; }
    std::size_t shots() const { return shots_; }
    std::size_t player_index() const { return player_; }
    int health() const { return health_; }
    int maximum_health() const { return 100; }
    int ammunition() const { return ammunition_; }
    int magazine_size() const { return tuning_.magazine; }
    int reserve_ammunition() const { return reserve_; }
    float recoil_amount() const { return recoil_; }
    bool reloading() const { return reload_timer_ > 0.f; }
    bool alive() const { return health_ > 0; }
    bool won() const { return total_ > 0 && hits_ == total_; }
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }

private:
    struct Moving {
        std::size_t index;
        Vector3 origin;
        float phase;
    };
    FpsTuning tuning_;
    std::size_t player_ = 0;
    float yaw_ = 0.f;
    float pitch_ = 0.f;
    float recoil_ = 0.f;
    float shot_timer_ = 0.f;
    float reload_timer_ = 0.f;
    float damage_timer_ = 0.f;
    float elapsed_ = 0.f;
    int health_ = 100;
    int ammunition_ = 12;
    int reserve_ = 60;
    std::size_t hits_ = 0;
    std::size_t total_ = 0;
    std::size_t shots_ = 0;
    std::vector<std::uint8_t> hit_;
    std::vector<Moving> moving_;
    std::vector<std::size_t> moving_indices_;
};

} // namespace northstarcpp
