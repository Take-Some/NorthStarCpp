#include <northstarcpp/skeleton.hpp>
#include "asset_list_file.hpp"
#include <boost/property_tree/xml_parser.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace northstarcpp {
namespace {
using namespace shared_assets::detail;
using namespace math;

JointTransform transform(float tx, float ty, float tz, float x, float y, float z,
                         float w, float sx, float sy, float sz)
{
    for (auto value : {tx,ty,tz,x,y,z,w,sx,sy,sz})
        require(std::isfinite(value) && std::abs(value) <= 10000.f, "Invalid joint transform");
    require(std::abs(sx) > 1e-5f && std::abs(sy) > 1e-5f && std::abs(sz) > 1e-5f,
        "Singular joint scale");
    quaternion q(float4{x,y,z,w});
    require(q.magnitude() > 0.001f, "Invalid joint quaternion");
    return {{tx,ty,tz}, q.normalize(), {sx,sy,sz}};
}

float4x4 local_matrix(JointTransform const& value)
{
    return create_scale(value.scale) * create_matrix(value.rotation, value.position);
}

std::string name_at(Bytes const& bytes, std::uint64_t base, std::uint64_t limit,
                    std::uint64_t offset)
{
    require(base <= limit && offset < limit - base, "Invalid clip name offset");
    auto const len = std::min<std::uint64_t>(limit - base - offset, 257);
    auto const* first = bytes.at(base + offset, len);
    auto const* end = std::find(first, first + len, 0);
    require(end != first + len, "Unterminated clip name");
    std::string name(first, end);
    validate_name(name);
    return name;
}
}

Skeleton load_skeleton(AssetStore const& assets, std::string const& path)
{
    using namespace shared_assets::detail;
    auto file = read_list_file(assets, path);
    require(file.kind == 10 && file.entries == 1 && file.body.size() <= 8 * 1024 * 1024,
        "Expected a YMT skeleton");
    std::istringstream input(std::string(file.body.begin(), file.body.end()));
    Tree xml;
    boost::property_tree::read_xml(input, xml, boost::property_tree::xml_parser::no_comments);
    auto const& root = xml.get_child("Metadata.Skeleton");
    require(root.get<std::string>("<xmlattr>.source_format") == "northstar.pc.joint_hierarchy.v1",
        "Unsupported skeleton hierarchy");
    Skeleton result;
    std::vector<math::float4x4> globals;
    std::unordered_set<std::string> names;
    for (auto const& node : root) {
        if (node.first == "<xmlattr>") continue;
        if (node.first == "Anchors") {
            result.eye_height = node.second.get<float>("<xmlattr>.eye_height", result.eye_height);
            continue;
        }
        require(node.first == "Joint" && result.joints.size() < 4096, "Invalid skeleton joint table");
        auto const& attr = node.second.get_child("<xmlattr>");
        auto const index = static_cast<int>(result.joints.size());
        auto parent = attr.get<int>("parent_index");
        require(attr.get<int>("index") == index && attr.get<int>("tag") == index &&
            (parent == -1 || (parent >= 0 && parent < index)),
            "Invalid joint hierarchy or tag");
        auto name = attr.get<std::string>("name");
        validate_name(name);
        require(names.insert(name).second, "Duplicate joint name");
        auto const f = [&](char const* name) { return attr.get<float>(name); };
        auto bind = transform(f("tx"), f("ty"), f("tz"), f("qx"), f("qy"), f("qz"),
            f("qw"), f("sx"), f("sy"), f("sz"));
        auto global = local_matrix(bind);
        if (parent >= 0) global = global * globals[static_cast<std::size_t>(parent)];
        math::float4x4 inverse;
        require(inverse.try_invert(global), "Cannot invert skeleton bind matrix");
        globals.push_back(global);
        result.inverse_bind.push_back(inverse);
        result.joints.push_back({std::move(name), parent, bind});
    }
    require(!result.joints.empty() && result.eye_height >= 0.1f && result.eye_height <= 20.f,
        "Invalid skeleton or eye anchor");
    return result;
}

std::vector<SkeletalClip> load_skeletal_clips(AssetStore const& assets, std::string const& path,
    std::vector<std::string> const& names, Skeleton const& skeleton)
{
    using namespace shared_assets::detail;
    auto file = read_list_file(assets, path);
    Bytes const bytes(file.body);
    require(file.kind == 11 && file.entries > 0 && file.entries <= 4096,
        "Expected a YCD clip dictionary");
    auto const version = bytes.integer(0,4);
    auto const table = bytes.integer(8,8);
    auto const strings = bytes.integer(16,8);
    auto const payload = bytes.integer(32,8);
    require((version == 2 || version == 3) && bytes.integer(4,4) == file.entries &&
        table == 48 && strings >= table + file.entries * 64 && strings <= payload &&
        payload <= bytes.size(), "Invalid YCD clip dictionary tables");
    std::vector<SkeletalClip> result;
    for (std::size_t entry = 0; entry < file.entries; ++entry) {
        auto const record = table + 64 * entry;
        auto name = name_at(bytes, strings, payload, bytes.integer(record + 8,4));
        if (std::find(names.begin(), names.end(), name) == names.end()) continue;
        SkeletalClip clip;
        clip.name = std::move(name);
        auto const tracks = bytes.integer(record + 16,4);
        clip.frames = static_cast<std::uint32_t>(bytes.integer(record + 20,4));
        auto const duration = bytes.real(record + 24);
        clip.sample_rate = bytes.real(record + 28);
        auto const loop = bytes.integer(record + 32,4);
        clip.loop = loop == 1;
        auto const start = bytes.integer(record + 40,8);
        auto const size = bytes.integer(record + 48,8);
        require(tracks > 0 && tracks <= skeleton.joints.size() && clip.frames >= 2 &&
            clip.frames <= 12000 && tracks * clip.frames <= 1500000 &&
            clip.sample_rate >= 1.f && clip.sample_rate <= 240.f && loop <= 1 &&
            std::abs(duration - (clip.frames - 1) / clip.sample_rate) < 0.02f &&
            start >= payload && size == tracks * (4 + 40ull * clip.frames),
            "Invalid YCD clip layout");
        bytes.at(start, size);
        std::unordered_set<std::uint32_t> unique;
        for (std::uint64_t t = 0; t < tracks; ++t) {
            auto const id = static_cast<std::uint32_t>(bytes.integer(start + 4*t,4));
            require(id < skeleton.joints.size() && unique.insert(id).second, "Invalid YCD joint index");
            clip.joints.push_back(id);
        }
        auto sample = start + tracks * 4;
        clip.samples.reserve(static_cast<std::size_t>(tracks * clip.frames));
        for (std::uint64_t i = 0; i < tracks * clip.frames; ++i, sample += 40) {
            auto const f = [&](unsigned n) { return bytes.real(sample + 4 * n); };
            clip.samples.push_back(transform(f(0),f(1),f(2),f(3),f(4),f(5),f(6),f(7),f(8),f(9)));
        }
        result.push_back(std::move(clip));
    }
    require(result.size() == names.size(), "Required YCD animation clip not found");
    return result;
}

SkeletonPose::SkeletonPose(Skeleton const& skeleton)
    : global_(skeleton.joints.size()), palette_(skeleton.joints.size())
{
    local_.reserve(skeleton.joints.size());
    for (auto const& joint : skeleton.joints) local_.push_back(joint.bind);
    evaluate(skeleton);
}

void SkeletonPose::evaluate(Skeleton const& skeleton)
{
    for (std::size_t i = 0; i < local_.size(); ++i) {
        auto const parent = skeleton.joints[i].parent;
        auto const local = local_matrix(local_[i]);
        global_[i] = parent < 0 ? local : local * global_[static_cast<std::size_t>(parent)];
        palette_[i] = skeleton.inverse_bind[i] * global_[i];
    }
}

void SkeletonPose::sample(Skeleton const& skeleton, SkeletalClip const& clip,
    float seconds, bool in_place)
{
    using namespace math;
    require(std::isfinite(seconds) && seconds >= 0, "Invalid skeletal playback time");
    float const last = static_cast<float>(clip.frames - 1);
    float const frame = clip.loop ? std::fmod(seconds * clip.sample_rate, last)
                                  : std::min(seconds * clip.sample_rate, last);
    auto const first = static_cast<std::size_t>(frame);
    auto const next = std::min(first + 1, static_cast<std::size_t>(clip.frames - 1));
    auto const fraction = frame - static_cast<float>(first);
    for (std::size_t i = 0; i < local_.size(); ++i) local_[i] = skeleton.joints[i].bind;
    for (std::size_t i = 0; i < clip.joints.size(); ++i) {
        auto const& a = clip.samples[first * clip.joints.size() + i];
        auto const& b = clip.samples[next * clip.joints.size() + i];
        auto& joint = local_[clip.joints[i]];
        joint = {a.position + (b.position - a.position) * fraction,
                 slerp(a.rotation, b.rotation, fraction),
                 a.scale + (b.scale - a.scale) * fraction};
        if (in_place && clip.joints[i] == 0) {
            joint.position.x -= a.position.x + (b.position.x - a.position.x) * fraction - clip.samples[i].position.x;
            joint.position.z -= a.position.z + (b.position.z - a.position.z) * fraction - clip.samples[i].position.z;
        }
    }
    evaluate(skeleton);
}
}
