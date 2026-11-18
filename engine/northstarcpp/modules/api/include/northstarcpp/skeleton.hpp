#pragma once
#include <northstarcpp/math.hpp>
#include <array>
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace northstarcpp {
class AssetStore;

struct JointTransform {
    math::float3 position{0,0,0};
    math::quaternion rotation;
    math::float3 scale{1,1,1};
};
struct SkeletonJoint {
    std::string name;
    int parent = -1;
    JointTransform bind;
};
// Author-controlled secondary rig behavior inferred from matching bind
// hierarchies at import time, never reconstructed per animation frame.
struct SecondaryPaletteConstraint {
    std::uint32_t bone = 0;
    std::uint32_t source_a = 0, source_b = 0;
    float source_b_weight = 0.f; // [0,1], row-major affine palette blend
};
// Canonical arm mapping derived from validated bind hierarchy. Missing or
// ambiguous side evidence leaves the original rig mapping unchanged.
struct CanonicalHumanoidSkeleton {
    static constexpr std::size_t missing=static_cast<std::size_t>(-1);
    std::size_t pelvis=missing,spine=missing,chest=missing,neck=missing,head=missing;
    std::size_t l_clavicle=missing,l_upperarm=missing,l_forearm=missing,l_hand=missing;
    std::size_t r_clavicle=missing,r_upperarm=missing,r_forearm=missing,r_hand=missing;
    std::size_t l_hand_prop=missing,r_hand_prop=missing;
    std::array<math::float3,2> finger_axis{},thumb_axis{};
    std::array<bool,2> hand_basis_valid{false,false};
    bool valid=false,side_repaired=false,side_confirmed=false;
};
struct Skeleton {
    std::vector<SkeletonJoint> joints;
    std::vector<math::float4x4> inverse_bind;
    // Bind-identical secondary rig nodes that must follow keyed primary joints.
    // Stored once by the importer to avoid rebuilding maps during each frame.
    std::vector<std::pair<std::uint32_t,std::uint32_t>> mirrored_helpers;
    std::vector<SecondaryPaletteConstraint> secondary_palette_constraints;
    // Built once during import: sorted indices enable allocation-free
    // joint lookups for IK and attachment sockets (manual rigs fall back).
    std::vector<std::uint32_t> joint_name_order;
    std::size_t joint_index(std::string_view name) const noexcept;
    CanonicalHumanoidSkeleton humanoid;
    float eye_height = 1.62f;
};
// Validate hierarchy, bind-pose side, and wrist-local finger/thumb directions.
CanonicalHumanoidSkeleton detect_canonical_humanoid_skeleton(Skeleton const&);
// Data-driven weapon finger pose, retargeted from source-rig joints at
// authoring time. Degrees describe LOCAL joint deltas, not world rotations.
struct FingerJointBend {
    std::string joint;
    math::float3 degrees{0.f,0.f,0.f};
};
struct SkeletalClip {
    std::string name;
    float sample_rate = 0;
    std::uint32_t frames = 0;
    bool loop = false;
    std::vector<std::uint32_t> joints;
    std::vector<JointTransform> samples;
};
// Read-only inspection and runtime share the exact same YMT validator.
Skeleton load_skeleton_from_bytes(std::vector<std::uint8_t> const& nef8);
Skeleton load_skeleton(AssetStore const&, std::string const& path);
std::vector<SkeletalClip> load_skeletal_clips(AssetStore const&, std::string const& path,
    std::vector<std::string> const& names, Skeleton const&);
// Decode preloaded YCD bytes without disk access on the frame thread.
std::vector<SkeletalClip> load_skeletal_clips_from_bytes(
    std::vector<std::uint8_t> const& nef8,
    std::vector<std::string> const& names, Skeleton const&);

class SkeletonPose {
public:
    explicit SkeletonPose(Skeleton const&);
    void sample(Skeleton const&, SkeletalClip const&, float seconds, bool in_place);
    // Retain the previous authored local pose for joints omitted by the new
    // YCD clip, then crossfade keyed joints from the outgoing clip.
    // This is evaluated before IK and prevents bind/T-pose flashes.
    void preserve_unkeyed_and_blend(Skeleton const&,
        std::vector<JointTransform> const& previous,
        std::vector<JointTransform> const& outgoing,float alpha);
    std::vector<JointTransform> const& local_transforms() const noexcept {
        return local_;
    }
    // Animation Graph bridge: accepts already-blended local TRS and keyed mask.
    // Maintains imported mirrored helpers and secondary skinning constraints.
    void set_local_transforms(Skeleton const&,
        std::vector<JointTransform> const& locals,
        std::vector<std::uint8_t> const& keyed);
    // Presentation-only CCD grip; never modifies the authored YCD clip.
    // Targets are in the same skeleton-local space as joints_world().
    bool aim_hand(Skeleton const&, char const* shoulder, char const* elbow,
                  char const* wrist, math::float3 target, unsigned iterations=5);

    // Anatomically constrained two-bone IK with an explicit elbow pole.
    // Keeps imported animation and mesh weights unchanged.
    bool pose_two_bone(Skeleton const&, char const* shoulder,
                       char const* elbow, char const* wrist,
                       math::float3 target, math::float3 elbow_pole,
                       float shoulder_limit=1.55f,
                       float elbow_limit=1.80f);

    // Rotation stage after two-bone positional IK. The target is an
    // orthonormal wrist orientation in skeleton/model space. Applying it only
    // to the wrist preserves elbow position and limb length.
    bool orient_wrist(Skeleton const&,char const* wrist,
                      math::float4x4 const& desired_global,
                      float max_angle_radians=1.30f);

    // One pass for all primary fingers plus bind-equivalent mirror joints.
    // Final palette includes secondary finger helper constraints after
    // the same single evaluation; no per-frame map/heap allocations.
    bool apply_finger_bends(Skeleton const&,
        std::vector<FingerJointBend> const& joints,float weight=1.f);

    // Bounded post-animation rig-space adjustment for pelvis/slope IK.
    // Delta is expressed in the bone parent's local space, not world space.
    // Physics-authoritative mode: suppress visual root translation so an
    // animation clip does not move the rig a second time over the Pawn.
    // Returns the removed local-space delta for a future motor handoff.
    std::optional<math::float3> consume_visual_root_motion(
        Skeleton const&,std::string_view root);
    bool adjust_local_joint(Skeleton const&,std::string_view joint,
        math::float3 offset,math::float3 axis,float radians);

    std::vector<math::float4x4> const& palette() const { return palette_; }
    std::vector<math::float4x4> const& joints_world() const { return global_; }
private:
    std::vector<JointTransform> local_;
    std::vector<math::float4x4> global_, palette_;
    // Persistent per-pose scratch; never allocate per animation sample.
    std::vector<std::uint8_t> keyed_;
    void evaluate(Skeleton const&);
};
}
