#pragma once

#include "viewport_character_self_collision.hpp"
#include "viewport_weapon_grip_profiles.hpp"
#include <northstarcpp/camera.hpp>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/skeleton.hpp>
#include <array>
#include <optional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace northstarcpp { class ProceduralIkController; class GroundedLocomotionController; class PhysicsScene; class DistributedLookAt; }
namespace northstarcpp::viewport_detail {

struct BoneAttachment {
    std::uint64_t parent=0;
    std::string bone;
    Vector3 offset{},rotation{};
    bool camera_forward=false;
};

struct GripView {
    float yaw,pitch;
    bool first_person;
    // Exact active camera eye. Null for the legacy/test-only anatomical
    // coordinate mode, which does not have an active FPS camera.
    std::optional<Vector3> eye;
    // Project-authored firearm location relative to the eye: metres.
    float anchor_forward=.52f, anchor_drop=.24f, anchor_right=.12f;
    // Immutable rig-owned source pose, imported once from WeaponLab XML.
    WeaponFingerProfile const* finger_profile=nullptr;
    bool aim_down_sights=false;
    // Render-only recoil. Never applied to hitscan or physical muzzle origin
    // until the next real published socket frame exists.
    float recoil_back=0.f, recoil_arm=0.f;
};

// A firearm grip is a full world-space hand transform, not a point IK.
// Wrist orientation comes from the weapon's authored primary/support grips;
// the elbow pole remains in the character body frame.
struct GripSolveTarget {
    Vector3 wrist_position{};
    math::quaternion wrist_rotation{};
    Vector3 elbow_pole{};
};

struct GripResult {
    Vector3 target,actual;
    bool solved;
};

struct PoseInput {
    Skeleton const& skeleton;
    SkeletalClip const& clip;
    float seconds;
    math::float4x4 const& mesh_world;
    math::float4x4 const& mesh_to_skeleton;
    std::optional<GripView> grip;
};

using GripResults = std::array<std::optional<GripResult>,2>;

// Inputs may be prepared on the host and evaluated against a privately owned
// pose. The evaluator never reads the ECS, touches VFS, or publishes sockets.
GripResults evaluate_pose(PoseInput const& input, SkeletonPose& output);
// Graph evaluator provides its own pre-sampled SkeletonPose; preserve weapon IK
// and all socket/grip behavior without replaying a legacy clip.
GripResults evaluate_prepared_pose(PoseInput const& input, SkeletonPose& output);

class SkeletalSceneFrame {
public:
    // Component strings are borrowed for the current immutable scene frame.
    void prepare(Scene const& scene);
    bool wants_hand_grip(std::uint64_t id) const;
    void publish_sockets(std::uint64_t id, Skeleton const& skeleton,
                         SkeletonPose const& evaluated,
                         math::float4x4 const& mesh_to_skeleton,
                         math::float4x4 const& mesh_world);
    std::optional<math::float4x4> attachment_transform(
        BoneAttachment const& attachment, CameraPose const* camera) const;

    // Opt-in native fallback for NPCs without a converted YCD; no global bind-pose fallback.
    std::unordered_set<std::uint64_t> procedural_humanoids;
    std::unordered_map<std::uint64_t,std::string_view> animations;
    // Opt-in XML Animation Graph. Legacy engine.model.animation stays valid.
    std::unordered_map<std::uint64_t,std::string_view> graphs;
    std::unordered_map<std::uint64_t,
        std::vector<std::pair<std::string,float>>> graph_parameters;
    // Opt-in, rig-authored IK data; the scene frame borrows component payloads.
    std::unordered_map<std::uint64_t,std::vector<std::string_view>> ik_chains;
    std::unordered_map<std::uint64_t,std::vector<std::string_view>> ik_targets;
    std::unordered_map<std::uint64_t,std::vector<std::string_view>> ik_contacts;
    std::unordered_map<std::uint64_t,std::vector<std::string_view>> ground_feet;
    std::unordered_map<std::uint64_t,std::string_view> ground_bodies;
    std::unordered_map<std::uint64_t,std::vector<std::string_view>> look_joints;
    std::unordered_map<std::uint64_t,std::string_view> look_targets;
    std::unordered_map<std::uint64_t,std::string_view> vehicle_seat_poses;
    std::unordered_map<std::uint64_t,std::vector<std::string_view>> vehicle_reaches;
    std::unordered_map<std::uint64_t,std::string_view> root_motion_policies;
    std::unordered_map<std::uint64_t,BoneAttachment> attachments;
    std::vector<std::size_t> order;
    bool has_cycles=false;

private:
    std::unordered_map<std::uint64_t,std::vector<std::string>> requests_;
    std::unordered_map<std::uint64_t,
        std::unordered_map<std::string,math::float4x4>> sockets_;
    std::unordered_map<std::uint64_t,std::size_t> indices_;
    std::unordered_map<std::uint64_t,CharacterSelfCollision> self_collisions_;
    // One parent per child. Store sibling chains in contiguous scratch.
    struct Links {
        std::size_t first,last,next;
        bool pending;
    };
    std::vector<Links> links_;
};

// Evaluate authored per-entity IK after sampling the animation and before
// weapon-specific grip IK and skinning. No physics or VFS calls are made.
void apply_procedural_ik(SkeletalSceneFrame const& frame,std::uint64_t entity,
    Skeleton const& rig,SkeletonPose& pose,
    std::unique_ptr<ProceduralIkController>& controller,
    std::vector<std::string>& definition_cache,float delta_seconds,
    PhysicsScene const* physics=nullptr,
    math::float4x4 const* rig_to_world=nullptr,
    std::unique_ptr<GroundedLocomotionController>* grounded=nullptr,
    std::vector<std::string>* foot_cache=nullptr,
    std::string* body_cache=nullptr,
    std::size_t ignored_body=static_cast<std::size_t>(-1));
// Optional rig-driven look-at, independent of the limb IK solver.
void apply_distributed_lookat(SkeletalSceneFrame const&,
    std::uint64_t entity,Skeleton const&,SkeletonPose&,
    math::float4x4 const& rig_to_world,float dt,
    std::unique_ptr<DistributedLookAt>& controller,
    std::vector<std::string>& config_cache);

} // namespace northstarcpp::viewport_detail
