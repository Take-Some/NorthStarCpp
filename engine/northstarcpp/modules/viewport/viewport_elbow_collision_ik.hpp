#pragma once
// Deterministic elbow-pole search on the exact two-bone reach circle.
// This is a viewport-space target generator; SkeletonPose remains the
// authoritative IK solver and never receives a translated wrist goal.
#include "viewport_character_self_collision.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace northstarcpp::viewport_detail {
struct CollisionAwareElbowPole {
    Vector3 position{};
    float predicted_intrusion=0.f;
    bool valid=false;
};

// Conservative screen-space clearance for the sleeve-bearing part of a
// forearm. The last 20% nearest the wrist is deliberately excluded: the
// gripping hand MUST remain close to a correctly aligned pistol. The
// clearance is measured from the true camera aim ray, not world Y or a
// renderer-specific projection. The radius/threshold is tuned for the
// skinned sleeve thickness, rather than treating the arm as a zero-radius
// bone line.
inline float ads_forearm_sight_clearance(Vector3 eye,Vector3 sight_forward,
                                         Vector3 elbow,Vector3 wrist) noexcept
{
    if(!eye.valid() || !sight_forward.valid() ||
       !elbow.valid() || !wrist.valid() ||
       sight_forward.magnitude()<.5f)return 0.f;
    const auto forward=sight_forward/sight_forward.magnitude();
    float closest=std::numeric_limits<float>::infinity();
    const auto segment=wrist-elbow;
    for(unsigned sample=0;sample<=8;++sample) {
        const float t=static_cast<float>(sample)*.10f;
        const auto point=elbow+segment*t;
        const auto relative=point-eye;
        const float depth=CharacterSelfCollision::dot(relative,forward);
        if(depth<.10f || depth>.85f)continue;
        const auto lateral=relative-forward*depth;
        closest=std::min(closest,lateral.magnitude());
    }
    return closest;
}

inline CollisionAwareElbowPole choose_collision_aware_elbow_pole(
    CharacterSelfCollision const& body, Vector3 shoulder, Vector3 wrist_target,
    Vector3 current_elbow, Vector3 preferred_pole, Vector3 outward,
    float upper_length, float lower_length, float clearance=.045f,
    float minimum_forearm_depth=0.f,Vector3 view_eye={},
    Vector3 view_forward={},float sight_guard=.235f) noexcept
{
    CollisionAwareElbowPole result{};
    if(!body.active || !shoulder.valid() || !wrist_target.valid() ||
       !current_elbow.valid() || !preferred_pole.valid() || !outward.valid() ||
       !std::isfinite(upper_length) || !std::isfinite(lower_length) ||
       upper_length<.04f || lower_length<.04f)return result;
    const auto difference=wrist_target-shoulder;
    const float unbounded= difference.magnitude();
    if(!std::isfinite(unbounded) || unbounded<.03f)return result;
    const auto axis=difference/unbounded;
    const float reach=std::clamp(unbounded,
        std::abs(upper_length-lower_length)+.003f,
        upper_length+lower_length-.003f);
    if(reach<=0.f)return result;
    const float along=(upper_length*upper_length-lower_length*lower_length+
                       reach*reach)/(2.f*reach);
    const float radius_sq=upper_length*upper_length-along*along;
    if(radius_sq<.000025f)return result;
    const float circle_radius=std::sqrt(std::max(0.f,radius_sq));
    const auto center=shoulder+axis*along;
    const auto planar=[&](Vector3 p) {
        return p-axis*CharacterSelfCollision::dot(p,axis);
    };
    auto outward_plane=planar(outward);
    if(outward_plane.magnitude()<.01f)return result;
    outward_plane.normalize();
    auto preferred=planar(preferred_pole-shoulder);
    if(preferred.magnitude()<.01f)preferred=planar(current_elbow-shoulder);
    if(preferred.magnitude()<.01f)preferred=outward_plane;
    preferred.normalize();
    auto perpendicular=math::cross_product(axis,preferred);
    if(perpendicular.magnitude()<.01f)return result;
    perpendicular.normalize();

    // The outward half of the reach circle is anatomically plausible; the
    // medial half often gives a crossed elbow even when IK error is tiny.
    // Evaluate the **predicted elbow position**, not the pole's position.
    // Penalize torso penetration more strongly than deviation from the
    // incoming animation, but preserve the closest clean natural bend.
    float best=std::numeric_limits<float>::infinity();
    for(unsigned sample=0;sample<32;++sample) {
        constexpr float tau=6.28318530717958647692f;
        const float angle=tau*static_cast<float>(sample)/32.f;
        const auto direction=preferred*std::cos(angle)+
                             perpendicular*std::sin(angle);
        const auto candidate=center+direction*circle_radius;
        const float lateral=CharacterSelfCollision::dot(
            candidate-shoulder,outward_plane);
        if(lateral<.010f)continue;
        const float intrusion=body.correction(candidate,clearance);
        const float shift=(candidate-current_elbow).magnitude();
        const float preference=(1.f-std::clamp(
            CharacterSelfCollision::dot(direction,preferred),-1.f,1.f));
        const float forearm_depth=CharacterSelfCollision::dot(
            wrist_target-candidate,body.forward);
        const float upperarm_depth=CharacterSelfCollision::dot(
            candidate-shoulder,body.forward);
        // A bent forearm viewed almost side-on eclipses the ADS sights even
        // when its elbow is clear of the torso. Preserve a forearm segment
        // pointing forward (toward the muzzle), and do not fold the upper arm
        // behind the shoulder. Disabled for non-ADS collision correction.
        if(minimum_forearm_depth>0.f &&
           (upperarm_depth<.070f ||
            wrist_target.y-candidate.y<.075f))
            continue; // never raise the elbow above the gun-grip wrist
        const float depth_penalty=minimum_forearm_depth>0.f?
            std::max(0.f,minimum_forearm_depth-forearm_depth)*8.f:0.f;
        const float sight_clearance=minimum_forearm_depth>0.f &&
            view_forward.magnitude()>.5f?
            ads_forearm_sight_clearance(view_eye,view_forward,
                                        candidate,wrist_target):
            std::numeric_limits<float>::infinity();
        // Move the elbows out of the first-person sight corridor, without
        // ever moving the hand off its actual firearm socket.
        const float view_penalty=minimum_forearm_depth>0.f?
            std::max(0.f,sight_guard-sight_clearance)*42.f:0.f;
        const float score=intrusion*120.f+
            shift*.12f+preference*.04f+
            std::max(0.f,.055f-lateral)*3.f+depth_penalty+view_penalty;
        if(score<best) {
            best=score;
            result.position=candidate;
            result.predicted_intrusion=intrusion;
            result.valid=true;
        }
    }
    return result;
}
} // namespace northstarcpp::viewport_detail
