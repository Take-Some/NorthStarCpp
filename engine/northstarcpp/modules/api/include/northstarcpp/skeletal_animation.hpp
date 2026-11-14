// NorthStarCpp 2026: native first-class skeletal animation and skinning.
// Matrix convention follows original NorthStar math: row vectors / row-major.
// GPU shaders must multiply mul(float4(position,1), palette[bone_index]).
#pragma once

#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/scene_animation.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace northstarcpp {

struct SkeletonBone {
    std::string name;
    std::int32_t parent = -1; // root=-1; others must reference earlier bones
    SceneAnimationPose bind{}; // local bind pose, rotations in degrees
};

// A single skinning influence uses at most four bone indices; influences
// should sum to 1, but small importer roundoff is normalized at runtime.
struct SkinInfluences {
    std::array<std::uint16_t,4> bone{};
    std::array<float,4> weight{1.f,0.f,0.f,0.f};
};

struct SkinnedVertex {
    MeshVertex vertex;
    SkinInfluences skin;
};

class SkeletonRig {
public:
    // Validates hierarchy, names, poses, and computes inverse bind transforms.
    // The original engine's native float4x4 implementation is reused.
    void set_bones(std::vector<SkeletonBone> bones);
    std::size_t bone_count() const noexcept { return bones_.size(); }
    std::vector<SkeletonBone> const& bones() const noexcept { return bones_; }
    std::vector<math::float4x4> const& inverse_bind() const noexcept {
        return inverse_bind_;
    }

    // One clip slot per bone. An empty clip retains the bind pose.
    // Returns inverseBind * animatedGlobal, in row-vector convention.
    std::vector<math::float4x4> skinning_palette(
        std::vector<SceneAnimationClip> const& tracks, float seconds) const;
private:
    std::vector<SkeletonBone> bones_;
    std::vector<math::float4x4> inverse_bind_;
};

// JSON rig version 1: bones=[{name,parent,translation,rotation,scale}, ...].
// The same bounded parser can be used by Player's VFS loader or Qt Editor.
SkeletonRig parse_skeleton_rig(std::string const& source);

// Explicit packed row-major float[16] matrices for GPU upload; caller owns
// returned memory and is responsible for graphics API synchronization.
std::vector<float> pack_skinning_palette(
    std::vector<math::float4x4> const& palette);

// CPU reference to validate and preview GPU-skinning output.
// Normal transforms use inverse-transpose; no actual GPU resources are created.
MeshAsset skin_mesh_cpu(std::vector<SkinnedVertex> const& vertices,
                        std::vector<std::uint32_t> const& indices,
                        std::vector<math::float4x4> const& palette);
} // namespace northstarcpp
