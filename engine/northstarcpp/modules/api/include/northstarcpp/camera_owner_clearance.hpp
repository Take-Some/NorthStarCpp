#pragma once
// Local-player camera/mesh clearance. Render cameras never alter the
// physics capsule, skinning weights, material visibility, or actor transform.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
namespace northstarcpp::camera_collision {
// Full-body FPS head clearance calibrated against the real WeaponLab pistol.
// Candidate adds 0.02 m: a 0.20 m radius places the eye 0.22 m ahead.
// The previous 0.54 m offset pushed the rear pistol sight behind the view.
inline constexpr float first_person_head_radius = 0.20f;
template<class V> inline float player_camera_distance_squared(V a,V b) noexcept {
    const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;
    return x*x+y*y+z*z;
}
template<class V> inline V player_camera_closest_point(V p,V a,V b) noexcept {
    const float h=b.y-a.y;
    const float t=h>.00001f?std::clamp((p.y-a.y)/h,0.f,1.f):0.f;
    return {a.x,a.y+t*h,a.z};
}
template<class V> inline bool player_camera_inside_capsule(
    V camera,V feet,float eye_height,float radius) noexcept {
    if(!std::isfinite(camera.x)||!std::isfinite(camera.y)||
       !std::isfinite(camera.z)||!std::isfinite(eye_height)||
       !std::isfinite(radius)||radius<=0.f)return false;
    const V bottom{feet.x,feet.y+std::min(.80f,eye_height),feet.z};
    const V top{feet.x,feet.y+eye_height,feet.z};
    return player_camera_distance_squared(camera,
        player_camera_closest_point(camera,bottom,top)) < radius*radius;
}
template<class V,class Allowed> inline V keep_camera_outside_player(
    V camera,V feet,float eye_height,float yaw,float radius,
    bool first_person,Allowed const& allowed) {
    if(!std::isfinite(yaw)||!std::isfinite(radius)||radius<=0.f ||
       !player_camera_inside_capsule(camera,feet,eye_height,radius))
        return camera;
    const V bottom{feet.x,feet.y+std::min(.80f,eye_height),feet.z};
    const V top{feet.x,feet.y+eye_height,feet.z};
    const V closest=player_camera_closest_point(camera,bottom,top);
    const float fx=std::sin(yaw),fz=std::cos(yaw);
    float dx=camera.x-closest.x,dz=camera.z-closest.z;
    const float d=std::hypot(dx,dz);
    // If the eye is in the head, never use an undefined radial direction.
    // FPS goes in front of the face; third person stays behind the character.
    if(first_person) { dx=fx;dz=fz; }
    else if(d<1.e-4f) { dx=-fx;dz=-fz; }
    else {dx/=d;dz/=d;}
    const float side=first_person?1.f:-1.f;
    const std::array<std::array<float,2>,7> directions{{
        {{dx,dz}},{{side*fx,side*fz}},{{-fz,fx}},{{fz,-fx}},
        {{.70710678f*(side*fx-fz),.70710678f*(side*fz+fx)}},
        {{.70710678f*(side*fx+fz),.70710678f*(side*fz-fx)}},
        {{0.f,0.f}}
    }};
    for(std::size_t i=0;i<directions.size();++i) {
        const auto v=directions[i];
        const V candidate=i+1==directions.size()?
            V{closest.x,top.y+radius+.02f,closest.z}:
            V{closest.x+v[0]*(radius+.02f),camera.y,
              closest.z+v[1]*(radius+.02f)};
        if(!player_camera_inside_capsule(candidate,feet,eye_height,radius) &&
           allowed(candidate))
            return candidate;
    }
    // No free nearby camera location: do not pretend that clipping or
    // a hidden head repairs a fully enclosed collision configuration.
    return camera;
}
} // namespace northstarcpp::camera_collision
