#pragma once
// Provider-independent two-stage FPS ballistic solution.
// The camera selects a target, but hit detection and effects originate at the
// real muzzle supplied by the caller. This header is usable from C++ hosts;
// the shipped @northstar/engine WeaponBallistics implements the same contract.
#include <northstarcpp/scene.hpp>
#include <optional>
#include <cmath>
#include <algorithm>
#include <cstddef>

namespace northstarcpp::ballistics {
struct TraceHit {
    std::size_t index=0;
    float distance=0.f;
};
struct Shot {
    Vector3 muzzle{},aim_point{},direction{};
    float travel=0.f;
    std::optional<TraceHit> camera_hit{},hit{};
    bool blocked=false;
    bool muzzle_authoritative=false;
};
inline bool finite(Vector3 p) noexcept {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
// Query(origin, normalized_direction, max_distance) -> closest valid hit.
// The caller chooses what to ignore and how to query the active physics
// provider; this code NEVER invents a weapon socket offset.
template<class Query>
std::optional<Shot> solve(Vector3 eye,Vector3 camera_direction,
    Vector3 muzzle,float range,Query&& query,bool authoritative=true)
{
    if(!finite(eye)||!finite(camera_direction)||!finite(muzzle)||
       !std::isfinite(range)||range<=0.f||range>100000.f)
        return std::nullopt;
    const float magnitude=camera_direction.magnitude();
    if(!std::isfinite(magnitude)||magnitude<.0001f)return std::nullopt;
    const auto aim=camera_direction/magnitude;
    Shot result{};
    result.muzzle=muzzle;
    result.muzzle_authoritative=authoritative;
    result.camera_hit=query(eye,aim,range);
    const auto distance=result.camera_hit?
        std::clamp(result.camera_hit->distance,0.f,range):range;
    result.aim_point=eye+aim*distance;
    const auto to_target=result.aim_point-muzzle;
    const auto length=to_target.magnitude();
    if(!std::isfinite(length)||length<.001f) {
        result.direction=aim;
        result.blocked=true;
        return result;
    }
    result.direction=to_target/length;
    if(result.direction.x*aim.x+result.direction.y*aim.y+
       result.direction.z*aim.z<=0.f) {
        result.blocked=true;
        return result;
    }
    // If the sight hits a surface, shoot only as far as that aim point.
    // Otherwise use the weapon's effective range from the muzzle.
    result.travel=result.camera_hit?length:range;
    result.hit=query(muzzle,result.direction,result.travel);
    return result;
}
}
