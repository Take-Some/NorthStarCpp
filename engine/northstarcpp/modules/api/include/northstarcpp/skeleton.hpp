#pragma once
#include <northstarcpp/math.hpp>
#include <array>
#include <cstdint>
#include <string>
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
struct Skeleton {
    std::vector<SkeletonJoint> joints;
    std::vector<math::float4x4> inverse_bind;
    // Bind-identical secondary rig nodes that must follow keyed primary joints.
    // Stored once by the importer to avoid rebuilding maps during each frame.
    std::vector<std::pair<std::uint32_t,std::uint32_t>> mirrored_helpers;
    std::vector<SecondaryPaletteConstraint> secondary_palette_constraints;
    float eye_height = 1.62f;
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

class SkeletonPose {
public:
    explicit SkeletonPose(Skeleton const&);
    void sample(Skeleton const&, SkeletalClip const&, float seconds, bool in_place);
    std::vector<math::float4x4> const& palette() const { return palette_; }
private:
    std::vector<JointTransform> local_;
    std::vector<math::float4x4> global_, palette_;
    void evaluate(Skeleton const&);
};
}
