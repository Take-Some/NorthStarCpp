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
struct Skeleton {
    std::vector<SkeletonJoint> joints;
    std::vector<math::float4x4> inverse_bind;
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
