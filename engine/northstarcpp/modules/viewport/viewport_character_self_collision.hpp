#pragma once
// Generic, allocation-free self-collision proxy for an animated humanoid.
// The narrow phase protects IK wrist/elbow goals and an equipped object's
// forward segment against torso/head capsules. This is not a Jolt rigid body.
// Capsule radii are derived from authored shoulder width; no actor names.
#include <northstarcpp/scene.hpp>
#include <algorithm>
#include <cmath>

namespace northstarcpp::viewport_detail {
struct SelfCollisionCapsule {
    Vector3 a{},b{};
    float radius=0.f;
};
struct CharacterSelfCollision {
    SelfCollisionCapsule torso{},head{};
    Vector3 forward{};
    bool active=false;
    static float dot(Vector3 a,Vector3 b) noexcept {
        return a.x*b.x+a.y*b.y+a.z*b.z;
    }
    static Vector3 nearest(SelfCollisionCapsule const& capsule,Vector3 point) noexcept {
        auto const segment=capsule.b-capsule.a;
        auto const sq=dot(segment,segment);
        auto const t=sq>1.e-7f?
            std::clamp(dot(point-capsule.a,segment)/sq,0.f,1.f):0.f;
        return capsule.a+segment*t;
    }
    static float front_clearance(SelfCollisionCapsule const& capsule,
                                 Vector3 point,Vector3 front,float clearance) noexcept {
        if(capsule.radius<=0.f)return 0.f;
        auto const from=point-nearest(capsule,point);
        const float projected=dot(from,front);
        const float lateral=std::max(0.f,dot(from,from)-projected*projected);
        const float radius=capsule.radius+clearance;
        if(lateral>=radius*radius)return 0.f;
        return std::max(0.f,std::sqrt(radius*radius-lateral)-projected);
    }
    float correction(Vector3 point,float clearance) const noexcept {
        if(!active)return 0.f;
        return std::max(front_clearance(torso,point,forward,clearance),
                        front_clearance(head,point,forward,clearance));
    }
    // Keep the proximal gun body and its barrel outside the actor while
    // moving BOTH hands with the grip. The barrel extends from the socket.
    float weapon_correction(Vector3 primary,Vector3 aim,
                            float length,float radius) const noexcept {
        auto const half=primary+aim*(length*.5f);
        auto const tip=primary+aim*length;
        return std::max({correction(primary,radius),
                         correction(half,radius),correction(tip,radius)});
    }
    Vector3 outside(Vector3 point,float clearance) const noexcept {
        const float overlap=correction(point,clearance);
        return overlap>0.f?point+forward*(overlap+.001f):point;
    }
};
inline CharacterSelfCollision make_character_self_collision(
    Vector3 pelvis,Vector3 chest,Vector3 neck,Vector3 head,
    Vector3 right_shoulder,Vector3 left_shoulder,Vector3 body_forward) noexcept {
    CharacterSelfCollision result;
    const float width=(right_shoulder-left_shoulder).magnitude();
    const float forward_len=body_forward.magnitude();
    if(!std::isfinite(width)||width<.18f||width>1.2f||
       !std::isfinite(forward_len)||forward_len<.02f)return result;
    const auto planar=Vector3{body_forward.x,0.f,body_forward.z};
    const float planar_len=planar.magnitude();
    if(planar_len<.02f)return result;
    result.forward=planar/planar_len;
    result.torso={pelvis,(chest+neck)*.5f,
                  std::clamp(width*.62f,.16f,.35f)};
    result.head={head,head,std::clamp(width*.34f,.095f,.19f)};
    result.active=true;
    return result;
}
} // namespace northstarcpp::viewport_detail
