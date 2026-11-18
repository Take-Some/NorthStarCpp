// NorthStarCpp — local crowd steering on top of any path planner.
// Deliberately NOT ORCA/RVO2: bounded time-to-collision velocity sampling.
// Handles moving peers and vehicles without changing physical transforms.
// Jolt collision constraints remain authoritative.
#pragma once
#include <northstarcpp/scene.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace northstarcpp::navigation {
struct CrowdObstacle {
    Vector3 position{0.f,0.f,0.f};
    Vector3 velocity{0.f,0.f,0.f};
    float radius=.4f;
    bool cooperative=false;
    std::uint64_t id=0;
};
struct CrowdSteering {
    Vector3 direction{0.f,0.f,0.f};
    float velocity_scale=0.f; // [0,1] shared with native Motor speed
    bool slowed=false;
    std::size_t close_neighbors=0;
};
inline CrowdSteering steer_crowd(
    std::uint64_t own_id,
    Vector3 position,
    Vector3 desired_direction,
    float preferred_speed,
    float radius,
    std::vector<CrowdObstacle> const& neighbors,
    float horizon_seconds=1.7f) {
    CrowdSteering output;
    auto const finite=[](float v){return std::isfinite(v);};
    if(!finite(position.x)||!finite(position.z)||
       !finite(desired_direction.x)||!finite(desired_direction.z)||
       !finite(preferred_speed)||preferred_speed<0.f||
       preferred_speed>100.f||!finite(radius)||radius<=0.f||
       !finite(horizon_seconds)||horizon_seconds<.1f)
        return output;
    const float desired_length=std::hypot(
        desired_direction.x,desired_direction.z);
    if(desired_length<.0001f || preferred_speed<.001f)return output;
    const float x=desired_direction.x/desired_length;
    const float z=desired_direction.z/desired_length;
    const float horizon=std::clamp(horizon_seconds,.2f,4.f);
    // Only the nearest 12 neighbors are considered; this bounds O(N*M)
    // work in crowded scenes even if the caller supplies many entities.
    struct Nearby {
        CrowdObstacle obstacle;
        float distance;
    };
    std::vector<Nearby> close;
    close.reserve(12);
    for(auto const& obstacle:neighbors) {
        if(obstacle.id==own_id || !finite(obstacle.position.x) ||
           !finite(obstacle.position.z) || !finite(obstacle.velocity.x) ||
           !finite(obstacle.velocity.z) || !finite(obstacle.radius))
            continue;
        if(std::abs(obstacle.position.y-position.y)>1.8f)continue;
        const float dx=obstacle.position.x-position.x;
        const float dz=obstacle.position.z-position.z;
        const float dist=std::hypot(dx,dz);
        if(dist>std::max(4.f,preferred_speed*horizon+radius+
                             obstacle.radius+2.f))
            continue;
        Nearby sample{obstacle,dist};
        auto loc=std::lower_bound(close.begin(),close.end(),dist,
            [](Nearby const& a,float b){return a.distance<b;});
        if(close.size()<12)close.insert(loc,sample);
        else if(loc!=close.end()) {
            close.insert(loc,sample);
            close.pop_back();
        }
    }
    output.close_neighbors=close.size();
    if(close.empty()) {
        output.direction={x,0.f,z};
        output.velocity_scale=1.f;
        return output;
    }
    constexpr float pi=3.14159265358979323846f;
    float best_cost=std::numeric_limits<float>::infinity();
    Vector3 chosen{0.f,0.f,0.f};
    float chosen_scale=0.f;
    // Prioritize forward motion; sample symmetrical detours with a stable
    // id-based passing preference to avoid two agents mirroring each other.
    for(int speed_band=0;speed_band<3;++speed_band) {
        const float fraction=speed_band==0?1.f:speed_band==1?.52f:0.f;
        for(int turn=-8;turn<=8;++turn) {
            if(fraction==0.f && turn!=0)continue;
            const float angle=float(turn)*pi/16.f;
            const float sn=std::sin(angle),cs=std::cos(angle);
            const float vx=(x*cs-z*sn)*preferred_speed*fraction;
            const float vz=(x*sn+z*cs)*preferred_speed*fraction;
            float cost=3.f*(1.f-fraction)*(1.f-fraction)+
                       .016f*float(turn*turn);
            // Symmetric encounters choose reproducible opposing passing
            // directions, rather than both selecting the same side.
            if(turn!=0 && (own_id&1u ? turn>0 : turn<0))
                cost+=.085f;
            bool dangerous=false;
            for(auto const& item:close) {
                auto const& p=item.obstacle.position;
                auto const& other=item.obstacle.velocity;
                const float dx=p.x-position.x,dz=p.z-position.z;
                const float r=radius+item.obstacle.radius+.13f;
                const float rvx=vx-other.x,rvz=vz-other.z;
                const float r2=rvx*rvx+rvz*rvz;
                float t=0.f;
                if(r2>1e-5f)
                    t=std::clamp((dx*rvx+dz*rvz)/r2,0.f,horizon);
                const float closest=std::hypot(dx-rvx*t,dz-rvz*t);
                const float now=std::hypot(dx,dz);
                // Agents already overlapping can only escape by moving
                // apart. Never select a velocity that worsens overlap.
                if(now<r && dx*rvx+dz*rvz>-.001f) {
                    dangerous=true;break;
                }
                if(closest<r && t>.0001f) {
                    dangerous=true;break;
                }
                const float comfort=r+.55f;
                if(closest<comfort)
                    cost+=.5f*(comfort-closest)*(comfort-closest);
            }
            if(dangerous)continue;
            if(cost<best_cost) {
                best_cost=cost;
                chosen={vx,0.f,vz};
                chosen_scale=fraction;
            }
        }
    }
    if(!std::isfinite(best_cost)) {
        output.slowed=true;
        return output; // wait; do not choose an unsafe penetration velocity
    }
    if(chosen_scale>.001f) {
        output.direction={chosen.x/(preferred_speed*chosen_scale),0.f,
                          chosen.z/(preferred_speed*chosen_scale)};
    }
    output.velocity_scale=chosen_scale;
    output.slowed=chosen_scale<.99f ||
        std::abs(output.direction.x-x)>.01f ||
        std::abs(output.direction.z-z)>.01f;
    return output;
}
} // namespace northstarcpp::navigation
