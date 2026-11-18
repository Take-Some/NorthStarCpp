#pragma once
// Native project-opt-in humanoid NPC pose when a character's original source
// animations have not yet been converted into playable YCD. This is NOT a
// replacement for source-authored animation or an implicit global fallback.
// Every sampled frame starts from the imported YMT bind joint transforms,
// applies bounded joint rotations, and evaluates *one* coherent skin palette.
#include <northstarcpp/skeleton.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace northstarcpp::viewport_detail {
inline void sample_npc_procedural_pose(
    Skeleton const& rig,SkeletonPose& pose,
    float seconds,float gait_phase,float horizontal_speed)
{
    auto const count=rig.joints.size();
    std::vector<JointTransform> local;
    local.reserve(count);
    for(auto const& joint:rig.joints)local.push_back(joint.bind);
    std::vector<std::uint8_t> keyed(count,0u);
    auto const absent=CanonicalHumanoidSkeleton::missing;
    auto find=[&](std::initializer_list<std::string_view> aliases)->std::size_t {
        for(auto const name:aliases) {
            auto const n=rig.joint_index(name);
            if(n<count)return n;
        }
        return absent;
    };
    auto rotate=[&](std::size_t joint,math::float3 axis,float angle) {
        if(joint==absent || joint>=count || !std::isfinite(angle) ||
           std::abs(angle)>1.65f)return;
        if(std::abs(angle)<1.e-6f)return;
        math::quaternion const delta(axis,angle);
        local[joint].rotation=(local[joint].rotation*delta).normalize();
        keyed[joint]=1;
    };
    auto const& h=rig.humanoid;
    // Importer-vetted anatomical side map, never assume names are correct
    // merely because they contain left/right text.
    auto const rightArm=h.valid?h.r_upperarm:find({"r_shoulder","r_upperarm","upperarm_r"});
    auto const leftArm=h.valid?h.l_upperarm:find({"l_shoulder","l_upperarm","upperarm_l"});
    auto const rightElbow=h.valid?h.r_forearm:find({"r_elbow","r_forearm","lowerarm_r"});
    auto const leftElbow=h.valid?h.l_forearm:find({"l_elbow","l_forearm","lowerarm_l"});
    auto const leftThigh=find({"l_upper_leg","l_hip","l_thigh","l_upperleg","thigh_l","upperleg_l"});
    auto const rightThigh=find({"r_upper_leg","r_hip","r_thigh","r_upperleg","thigh_r","upperleg_r"});
    auto const leftKnee=find({"l_knee","l_calf","l_lowerleg","calf_l","lowerleg_l"});
    auto const rightKnee=find({"r_knee","r_calf","r_lowerleg","calf_r","lowerleg_r"});
    auto const spine=h.spine!=absent?h.spine:find({"spineb","spine","spine_01"});
    auto const head=h.head!=absent?h.head:find({"headb","head"});
    auto const idle_breath=std::sin(seconds*1.85f);
    auto const idle_sway=std::sin(seconds*.74f);
    // Smooth 0..1 weight driven from actual displacement of the scene body.
    auto const motion=std::clamp(horizontal_speed/.85f,0.f,1.f);
    auto const swing=std::sin(gait_phase);
    auto const opposite=std::sin(gait_phase+3.14159265f);
    // Lower outward-facing T-pose upper arms to a relaxed human silhouette.
    // This offset is persistent for *all* frames and retained on rotation.
    rotate(leftArm,{0.f,0.f,1.f},1.02f);
    rotate(rightArm,{0.f,0.f,1.f},-1.02f);
    // Natural arm counter-swing while walking and subtle idle breaths.
    rotate(leftArm,{1.f,0.f,0.f},(.28f*swing*motion+.034f*idle_breath));
    rotate(rightArm,{1.f,0.f,0.f},(.28f*opposite*motion-.034f*idle_breath));
    // Slight elbow flex, never lock arms on the bind-pose straight line.
    rotate(leftElbow,{0.f,0.f,1.f},-.23f-.055f*motion);
    rotate(rightElbow,{0.f,0.f,1.f},.23f+.055f*motion);
    rotate(leftThigh,{1.f,0.f,0.f},.39f*opposite*motion);
    rotate(rightThigh,{1.f,0.f,0.f},.39f*swing*motion);
    rotate(leftKnee,{1.f,0.f,0.f},.16f*std::max(0.f,swing)*motion);
    rotate(rightKnee,{1.f,0.f,0.f},.16f*std::max(0.f,opposite)*motion);
    rotate(spine,{0.f,0.f,1.f},.022f*idle_sway+.032f*motion*opposite);
    rotate(head,{0.f,1.f,0.f},.065f*std::sin(seconds*.31f));
    // A short nonverbal right-hand greeting at a repeatable interval, but
    // only when stationary. It never reverts the torso or limbs to bind pose.
    auto const cycle=std::fmod(seconds+2.0f,8.0f);
    const float gesture=(cycle>2.f && cycle<4.4f)
        ?std::sin((cycle-2.f)/2.4f*3.14159265f)*(1.f-motion):0.f;
    rotate(rightArm,{1.f,0.f,0.f},-.65f*gesture);
    rotate(rightElbow,{1.f,0.f,0.f},-.42f*gesture);
    pose.set_local_transforms(rig,local,keyed);
}
} // namespace northstarcpp::viewport_detail
