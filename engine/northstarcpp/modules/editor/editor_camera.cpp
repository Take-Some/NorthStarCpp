// NorthStar Editor camera and geometric picking
// Copyright (c) 2026 Kayla's Systems — Kayla Verner.
#include "editor_camera.hpp"
#include "editor_tools.hpp"
#include <northstarcpp/camera_dynamics.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
namespace northstarcpp::editor_tools {
namespace {
constexpr float pi=3.14159265358979323846f;
float dot(Vector3 a,Vector3 b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
Vector3 cross(Vector3 a,Vector3 b) noexcept {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
Vector3 normalized(Vector3 a) noexcept {
    float const n=std::sqrt(dot(a,a));
    return n>1e-9f ? Vector3{a.x/n,a.y/n,a.z/n} : Vector3{0.f,0.f,1.f};
}
Vector3 plus(Vector3 a,Vector3 b) noexcept {return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vector3 mul(Vector3 a,float k) noexcept {return {a.x*k,a.y*k,a.z*k};}
}
Vector3 EditorCamera::forward() const noexcept {
    return {std::sin(pose.yaw)*std::cos(pose.pitch),std::sin(pose.pitch),
        std::cos(pose.yaw)*std::cos(pose.pitch)};
}
Vector3 EditorCamera::right() const noexcept {
    return {std::cos(pose.yaw),0.f,-std::sin(pose.yaw)};
}
Vector3 EditorCamera::up() const noexcept {return cross(forward(),right());}
void EditorCamera::look(float dx,float dy,float sensitivity) noexcept {
    if(!std::isfinite(dx)||!std::isfinite(dy))return;
    pose.yaw=std::remainder(pose.yaw+dx*sensitivity,2.f*pi);
    pose.pitch=std::clamp(pose.pitch-dy*sensitivity,-1.52f,1.52f);
}
void EditorCamera::drag_look(
    float pointer_dx,float pointer_dy,float sensitivity) noexcept
{
    // Positive Qt X means moving the cursor right: turn the camera right.
    // Qt Y grows down, and look() already converts it to upward-positive
    // pitch; do not invert either axis at this call site.
    look(pointer_dx,pointer_dy,sensitivity);
}
void EditorCamera::move(float sideways,float vertical,float forward_distance) noexcept {
    pose.position=plus(pose.position,plus(mul(right(),sideways),
        plus(mul(up(),vertical),mul(forward(),forward_distance))));
}
void EditorCamera::fly(float sideways,float world_up,
                       float forward_distance) noexcept {
    if(!std::isfinite(sideways)||!std::isfinite(world_up)||
       !std::isfinite(forward_distance))return;
    Vector3 const direction=plus(mul(right(),sideways),
        plus(Vector3{0.f,world_up,0.f},mul(forward(),forward_distance)));
    pose.position=plus(pose.position,direction);
}
bool EditorCamera::fly_damped(
    float sideways,float world_up,float forward_axis,
    float meters_per_second,float dt,float acceleration_seconds,
    float deceleration_seconds) noexcept {
    if(!std::isfinite(sideways)||!std::isfinite(world_up)||
       !std::isfinite(forward_axis)||!std::isfinite(meters_per_second)||
       !std::isfinite(dt)||!std::isfinite(acceleration_seconds)||
       !std::isfinite(deceleration_seconds) ||
       dt<=0.f || dt>0.25f || meters_per_second<0.f ||
       meters_per_second>10000.f || acceleration_seconds<0.f ||
       acceleration_seconds>10.f || deceleration_seconds<0.f ||
       deceleration_seconds>10.f)
        return false;
    auto const magnitude=std::hypot(sideways,world_up,forward_axis);
    if(!std::isfinite(magnitude))return false;
    auto const scale=magnitude>1.f?1.f/magnitude:1.f;
    // Exactly match the existing fly() basis: right, absolute up, forward.
    auto const direction=plus(mul(right(),sideways*scale),
        plus(Vector3{0.f,world_up*scale,0.f},
             mul(forward(),forward_axis*scale)));
    auto const wanted=mul(direction,meters_per_second);
    auto const finite_bounded=[](Vector3 v,float max_abs) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z) &&
               std::abs(v.x)<=max_abs && std::abs(v.y)<=max_abs &&
               std::abs(v.z)<=max_abs;
    };
    // A loaded scene or stale provider must never let invalid inertia
    // teleport the Editor viewport arbitrarily far outside the map.
    if(!finite_bounded(flight_velocity_,10000.f))flight_velocity_={};
    if(!finite_bounded(wanted,10000.f))return false;
    auto const tau=magnitude>0.f?acceleration_seconds:deceleration_seconds;
    auto const alpha=camera_dynamics::exponential_alpha(dt,tau);
    auto const before=flight_velocity_;
    flight_velocity_=plus(mul(before,1.f-alpha),mul(wanted,alpha));
    // Integrate the full velocity curve, rather than stepping using only
    // the end-of-frame velocity. Constant input stays consistent at 30/120 FPS.
    auto const offset=mul(plus(before,mul(wanted,-1.f)),tau*alpha);
    auto const displacement=plus(mul(wanted,dt),offset);
    if(magnitude==0.f && dot(flight_velocity_,flight_velocity_)<
                           1.e-8f)flight_velocity_={};
    auto const travelled=dot(displacement,displacement);
    if(!finite_bounded(flight_velocity_,10000.f) ||
       !finite_bounded(displacement,meters_per_second*dt+1.e-3f)) {
        flight_velocity_={};
        return false;
    }
    if(travelled>1.e-12f) {
        pose.position=plus(pose.position,displacement);
        return true;
    }
    return false;
}
void EditorCamera::dolly(float amount) noexcept {
    if(std::isfinite(amount))move(0.f,0.f,std::clamp(amount,-5000.f,5000.f));
}
void EditorCamera::frame(Vector3 target,float distance) noexcept {
    distance=std::clamp(distance,2.f,20000.f);
    pose.position=plus(target,mul(forward(),-distance));
}
std::optional<ScreenPoint> EditorCamera::project(Vector3 world,float width,float height) const noexcept {
    if(width<=0.f||height<=0.f)return {};
    Vector3 const delta{world.x-pose.position.x,world.y-pose.position.y,
                        world.z-pose.position.z};
    float const depth=dot(delta,forward());
    if(depth<=pose.near_plane)return {};
    float const focal=height/(2.f*std::tan(pose.fov_y_degrees*pi/360.f));
    return ScreenPoint{width*.5f+dot(delta,right())*focal/depth,
                       height*.5f-dot(delta,up())*focal/depth};
}
Ray EditorCamera::screen_ray(float x,float y,float width,float height) const noexcept {
    if(width<=0.f||height<=0.f)return {pose.position,forward()};
    float const focal=height/(2.f*std::tan(pose.fov_y_degrees*pi/360.f));
    auto const delta=plus(forward(),plus(mul(right(),(x-width*.5f)/focal),
                            mul(up(),(height*.5f-y)/focal)));
    return {pose.position,normalized(delta)};
}
std::optional<float> intersect_actor_proxy(Ray ray,SceneObject const& object,
                                           std::uint32_t version) noexcept {
    float const size=object.size;
    if(!std::isfinite(size)||size<=0.f)return {};
    // GPU's cube proxy is positioned on its base, not at its center.
    Vector3 const center{object.position.x,object.position.y+size*.5f,object.position.z};
    Vector3 const diff{center.x-ray.origin.x,center.y-ray.origin.y,center.z-ray.origin.z};
    float near=0.f,far=std::numeric_limits<float>::max();
    for(unsigned i=0;i<3;++i) {
        auto const axis=version==3 ?
            gizmo_axis(object,static_cast<GizmoAxis>(i),true) :
            (i==0?Vector3{1.f,0.f,0.f}:i==1?Vector3{0.f,1.f,0.f}:Vector3{0.f,0.f,1.f});
        float const scale=version==3 ?
            (i==0?object.node.scale.x:i==1?object.node.scale.y:object.node.scale.z) : 1.f;
        float const extent=std::max(.001f,size*.5f*std::abs(scale));
        float const projected=dot(diff,axis);
        float const velocity=dot(ray.direction,axis);
        if(std::abs(velocity)<1e-7f) {
            if(std::abs(projected)>extent)return {};
        } else {
            float a=(projected-extent)/velocity;
            float b=(projected+extent)/velocity;
            if(a>b)std::swap(a,b);
            near=std::max(near,a);
            far=std::min(far,b);
            if(near>far)return {};
        }
    }
    return far>=0.f?std::optional<float>(near):std::nullopt;
}
std::optional<std::size_t> pick_actor(Scene const& scene,Ray ray) noexcept {
    float closest=std::numeric_limits<float>::max();
    std::optional<std::size_t> chosen;
    for(std::size_t i=0;i<scene.objects.size();++i) {
        auto const& obj=scene.objects[i];
        if(scene.version==3&&!obj.node.visible)continue;
        auto const hit=intersect_actor_proxy(ray,obj,scene.version);
        if(hit && *hit<closest) {closest=*hit;chosen=i;}
    }
    return chosen;
}
} // namespace northstarcpp::editor_tools
