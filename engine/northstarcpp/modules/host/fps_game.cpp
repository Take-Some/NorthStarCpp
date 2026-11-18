// NorthStarCpp FPS gameplay
// Copyright (c) 2026 Kayla's Systems — Kayla Verner
#include <northstarcpp/fps_game.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/collision_queries.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace northstarcpp {
namespace {
constexpr float sensitivity = 0.0024f;
constexpr float max_pitch = 1.40f;
bool target(SceneObject const& o)
{
    return o.name.compare(0, 7, "Target ") == 0 &&
           o.name.compare(0, 12, "Target Base ") != 0;
}
bool moving_target(SceneObject const& o)
{
    return o.name.compare(0, 14, "Target Moving ") == 0;
}
bool hazard(SceneObject const& o)
{
    return o.name.compare(0, 7, "Hazard ") == 0;
}
}

FpsArena::FpsArena(Scene const& scene, FpsTuning tuning, bool allow_empty_targets)
    : tuning_(tuning), hit_(scene.objects.size(), 0)
{
    bool found = false;
    for (std::size_t i = 0; i < scene.objects.size(); ++i) {
        auto const& o = scene.objects[i];
        if (scene_pawn::is_local(scene,o)) {
            if (found) throw std::runtime_error("FPS arena requires exactly one player");
            player_ = i;
            found = true;
        }
        if (target(o)) ++total_;
        if (moving_target(o)) {
            if (o.mass != 0.f)
                throw std::runtime_error("Moving FPS targets must be static physics bodies");
            moving_.push_back({i, o.position, static_cast<float>(moving_.size()) * 1.9f});
            moving_indices_.push_back(i);
        }
    }
    ammunition_ = tuning_.magazine;
    reserve_ = tuning_.reserve;
    if (!found || (!total_ && !allow_empty_targets))
        throw std::runtime_error("FPS arena requires a player and at least one target");
}
void FpsArena::look(float dx, float dy, float sx, float sy, float dt)
{
    if (!std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(dt))
        throw std::runtime_error("Non-finite FPS camera input");
    if (!alive()) return;
    yaw_ += dx * sensitivity + sx * std::clamp(dt, 0.f, 0.05f) * 2.5f;
    pitch_ = std::clamp(pitch_ - dy * sensitivity +
        sy * std::clamp(dt, 0.f, 0.05f) * 2.1f, -max_pitch, max_pitch);
    if (std::abs(yaw_) > 3.14159265f)
        yaw_ = std::remainder(yaw_, 6.28318530718f);
}
void FpsArena::advance(float dt, Scene const& scene)
{
    if (!std::isfinite(dt) || dt < 0.f || dt > 0.25f)
        throw std::runtime_error("Invalid FPS simulation delta");
    if (scene.objects.size() < hit_.size())
        throw std::runtime_error("FPS scene topology changed");
    if (dt == 0.f) return;
    elapsed_ += dt;
    shot_timer_ = std::max(0.f, shot_timer_ - dt);
    damage_timer_ = std::max(0.f, damage_timer_ - dt);
    // Recover to the true sight direction using critically damped springs.
    // This is frame-rate independent even when the frame delta is clamped.
    auto spring=[dt](float& displacement,float& speed,float omega) {
        float const decay=std::exp(-omega*dt);
        float const c=speed+omega*displacement;
        speed=(speed-omega*c*dt)*decay;
        displacement=(displacement+c*dt)*decay;
        if(std::abs(displacement)<.00002f&&std::abs(speed)<.0002f)
            displacement=speed=0.f;
    };
    spring(recoil_pitch_,recoil_pitch_speed_,22.f);
    spring(recoil_yaw_,recoil_yaw_speed_,25.f);
    recoil_*=std::exp(-13.f*dt);
    if(recoil_<.0001f)recoil_=0.f;
    if (reload_timer_ > 0.f) {
        reload_timer_ = std::max(0.f, reload_timer_ - dt);
        if (reload_timer_ == 0.f) {
            int const taken = std::min(magazine_size() - ammunition_, reserve_);
            ammunition_ += taken;
            reserve_ -= taken;
        }
    }
    if (!alive() || damage_timer_ > 0.f) return;
    auto const& player = scene.objects[player_].position;
    for (auto const& o : scene.objects) {
        if (!hazard(o)) continue;
        float const radius = o.size * 0.5f + 0.90f; // reachable at collision contact
        if (std::abs(player.y - o.position.y) < 1.4f &&
            std::abs(player.x - o.position.x) < radius &&
            std::abs(player.z - o.position.z) < radius) {
            health_ = std::max(0, health_ - tuning_.hazard_damage);
            damage_timer_ = 0.8f;
            break;
        }
    }
}
Vector3 FpsArena::moving_position(std::size_t index) const
{
    for (auto const& m : moving_) {
        if (m.index == index)
            return {m.origin.x + std::sin(elapsed_ * 1.7f + m.phase) * 0.9f,
                    m.origin.y, m.origin.z};
    }
    throw std::out_of_range("FPS target is not animated");
}
Vector3 FpsArena::direction() const
{
    auto const sight_yaw=yaw(),sight_pitch=pitch();
    return {std::sin(sight_yaw) * std::cos(sight_pitch), std::sin(sight_pitch),
            std::cos(sight_yaw) * std::cos(sight_pitch)};
}
Vector3 FpsArena::eye(Scene const& scene) const
{
    if (scene.objects.size() < hit_.size())
        throw std::runtime_error("FPS scene topology changed");
    auto const& p = scene.objects[player_].position;
    return {p.x, p.y + 1.45f, p.z};
}
Vector3 FpsArena::movement(float strafe, float forward) const
{
    if (!std::isfinite(strafe) || !std::isfinite(forward))
        throw std::runtime_error("Invalid FPS movement");
    if (!alive()) return {0.f,0.f,0.f};
    float const x = std::clamp(strafe,-1.f,1.f);
    float const z = std::clamp(forward,-1.f,1.f);
    float const length = std::hypot(x,z);
    float const scale = length > 1.f ? 1.f / length : 1.f;
    return {(x * std::cos(yaw_) + z * std::sin(yaw_)) * scale, 0.f,
            (z * std::cos(yaw_) - x * std::sin(yaw_)) * scale};
}
bool FpsArena::fire(Scene const& scene,std::optional<Vector3> muzzle)
{
    if (!alive() || shot_timer_ > 0.f || reloading() || ammunition_ <= 0)
        return false;
    if (scene.objects.size() < hit_.size())
        throw std::runtime_error("FPS scene topology changed");
    // Snapshot before recoil. The actual muzzle is optional for the
    // target-only legacy arena, but required for attached weapon VFX.
    auto const ray=direction();
    auto const origin=eye(scene);
    auto const world_trace=[&](Vector3 start,Vector3 direction,
                              float maximum)->std::optional<ballistics::TraceHit> {
        std::optional<ballistics::TraceHit> closest;
        for(std::size_t i=0;i<scene.objects.size();++i) {
            if(i==player_ || scene.objects[i].node.layer=="Weapon debris")continue;
            auto const& object=scene.objects[i];
            const float half=object.size*.5f;
            math::aabb box;
            box.min={object.position.x-half,object.position.y,
                     object.position.z-half};
            box.max={object.position.x+half,object.position.y+object.size,
                     object.position.z+half};
            const auto hit=collision::ray_box_entry(box,start,direction,maximum);
            if(hit && (!closest || *hit<closest->distance))
                closest=ballistics::TraceHit{i,*hit};
        }
        return closest;
    };
    last_shot_=ballistics::solve(origin,ray,muzzle.value_or(origin),
        tuning_.weapon_range,world_trace,muzzle.has_value());
    if(!last_shot_)return false;
    --ammunition_;
    ++shots_;
    shot_timer_ = tuning_.fire_interval;
    recoil_=std::min(recoil_+.96f,1.6f);
    recoil_pitch_=std::min(recoil_pitch_+.031f,.13f);
    recoil_pitch_speed_=std::min(recoil_pitch_speed_+.24f,1.0f);
    recoil_yaw_+=((shots_%3)==0?-1.f:1.f)*.0035f;
    recoil_yaw_speed_+=((shots_%2)==0?-1.f:1.f)*.038f;
    // The final hit is authoritative from the real muzzle ray, never
    // from the eye/sight probe. Both ray origins are retained for diagnostics.
    auto const closest=last_shot_->hit?
        last_shot_->hit->index:scene.objects.size();
    if(last_shot_->blocked || closest>=hit_.size() ||
       !target(scene.objects[closest]) || hit_[closest])
        return false;
    hit_[closest] = 1;
    ++hits_;
    return true;
}
bool FpsArena::begin_reload()
{
    if (!alive() || reloading() || ammunition_ == magazine_size() || reserve_ == 0)
        return false;
    reload_timer_ = tuning_.reload_seconds;
    return true;
}
void FpsArena::reset()
{
    last_shot_.reset();
    std::fill(hit_.begin(),hit_.end(),0);
    hits_ = shots_ = 0;
    health_ = 100;
    ammunition_ = tuning_.magazine;
    reserve_ = tuning_.reserve;
    shot_timer_ = reload_timer_ = recoil_ = damage_timer_ = elapsed_ = 0.f;
    recoil_pitch_=recoil_pitch_speed_=0.f;
    recoil_yaw_=recoil_yaw_speed_=0.f;
    yaw_ = pitch_ = 0.f;
}
void FpsArena::tint(Scene& scene) const
{
    if (scene.objects.size() < hit_.size())
        throw std::runtime_error("FPS scene topology changed");
    for (std::size_t i = 0; i < hit_.size(); ++i)
        if (hit_[i]) scene.objects[i].color = tuning_.hit_color;
}
} // namespace northstarcpp
