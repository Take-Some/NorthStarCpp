#pragma once

#include <northstarcpp/camera.hpp>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/skeleton.hpp>
#include <array>
#include <optional>
#include <string_view>
#include <unordered_map>

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

    std::unordered_map<std::uint64_t,std::string_view> animations;
    std::unordered_map<std::uint64_t,BoneAttachment> attachments;
    std::vector<std::size_t> order;
    bool has_cycles=false;

private:
    std::unordered_map<std::uint64_t,std::vector<std::string>> requests_;
    std::unordered_map<std::uint64_t,
        std::unordered_map<std::string,math::float4x4>> sockets_;
    std::unordered_map<std::uint64_t,std::size_t> indices_;
    // One parent per child. Store sibling chains in contiguous scratch.
    struct Links {
        std::size_t first,last,next;
        bool pending;
    };
    std::vector<Links> links_;
};

} // namespace northstarcpp::viewport_detail
