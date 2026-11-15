#include <northstarcpp/skeleton.hpp>
#include "asset_list_file.hpp"
#include <boost/property_tree/xml_parser.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <string_view>

namespace northstarcpp {
namespace {
using namespace shared_assets::detail;
using namespace math;

JointTransform transform(float tx, float ty, float tz, float x, float y, float z,
                         float w, float sx, float sy, float sz,
                         bool animation_key=false)
{
    for (auto value : {tx,ty,tz,x,y,z,w,sx,sy,sz})
        require(std::isfinite(value) && std::abs(value) <= 10000.f, "Invalid joint transform");
    // A bind skeleton must be invertible. Weapon animation clips may hide
    // accessories by setting an animated key scale to exactly zero.
    require(animation_key ||
        (std::abs(sx)>1e-5f && std::abs(sy)>1e-5f && std::abs(sz)>1e-5f),
        "Singular joint scale");
    quaternion q(float4{x,y,z,w});
    require(q.magnitude() > 0.001f, "Invalid joint quaternion");
    // The imported YMT and YCD quaternion components use the opposite
    // handedness to NorthStarCpp's row-vector create_matrix convention.
    // Conjugate *both* bind and animation keyframes at the format boundary;
    // otherwise joint pivots do not align with the YDD skinned mesh.
    q.conjugate();
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
    // Skeletons are not necessarily humanoids: weapon and prop YMT rigs
    // legitimately define eye_height=0 (no first-person camera anchor).
    // Camera-equipped character rigs retain their authored positive height.
    require(!result.joints.empty() && std::isfinite(result.eye_height) &&
        result.eye_height >= 0.f && result.eye_height <= 20.f,
        "Invalid skeleton or eye anchor");
    std::unordered_map<std::string,std::uint32_t> by_name;
    by_name.reserve(result.joints.size());
    for(std::size_t i=0;i<result.joints.size();++i)
        by_name.emplace(result.joints[i].name,static_cast<std::uint32_t>(i));
    constexpr std::string_view suffix="_helper";
    auto close=[](float a,float b){return std::abs(a-b)<1e-4f;};
    auto bind_equal=[&](JointTransform const& a,JointTransform const& b){
        auto const& x=a.rotation,&y=b.rotation;
        return close(a.position.x,b.position.x) &&
               close(a.position.y,b.position.y) &&
               close(a.position.z,b.position.z) &&
               close(a.scale.x,b.scale.x) &&
               close(a.scale.y,b.scale.y) &&
               close(a.scale.z,b.scale.z) &&
               ((close(x.x,y.x)&&close(x.y,y.y)&&close(x.z,y.z)&&close(x.w,y.w))||
                (close(x.x,-y.x)&&close(x.y,-y.y)&&close(x.z,-y.z)&&close(x.w,-y.w)));
    };
    for(std::size_t i=0;i<result.joints.size();++i) {
        auto const& helper=result.joints[i];
        if(helper.name.size()<=suffix.size() ||
           helper.name.compare(helper.name.size()-suffix.size(),suffix.size(),suffix))
            continue;
        auto it=by_name.find(helper.name.substr(0,helper.name.size()-suffix.size()));
        if(it==by_name.end())continue;
        if(bind_equal(helper.bind,result.joints[it->second].bind))
            result.mirrored_helpers.emplace_back(static_cast<std::uint32_t>(i),it->second);
    }
    // The historical character rig may contain a second, unkeyed helper-root
    // with procedural twist/neck joints. Locomotion clips animate the primary
    // skeleton; independent helper transforms are not authored in the YCD.
    // Derive conservative anatomy constraints once from *validated bind-space*
    // bone positions. Never overwrite the 51 exact mirrored source joints.
    if(auto root_helper=by_name.find("root_helper");root_helper!=by_name.end()) {
        auto descendant=[&](std::uint32_t i,std::uint32_t ancestor) {
            for(int id=static_cast<int>(i);id>=0;
                id=result.joints[static_cast<std::size_t>(id)].parent)
                if(static_cast<std::uint32_t>(id)==ancestor)return true;
            return false;
        };
        auto mirrored=[&](std::uint32_t i) {
            return std::any_of(result.mirrored_helpers.begin(),
                               result.mirrored_helpers.end(),
                [&](auto const& pair){return pair.first==i;});
        };
        auto segment=[&](std::string const& helper,std::string const& begin,
                         std::string const& end) {
            auto a=by_name.find(helper), b=by_name.find(begin), c=by_name.find(end);
            if(a==by_name.end() || b==by_name.end() || c==by_name.end())
                return;
            auto const& start=globals[b->second];
            auto const& finish=globals[c->second];
            math::float3 direction{
                finish.elements[3][0]-start.elements[3][0],
                finish.elements[3][1]-start.elements[3][1],
                finish.elements[3][2]-start.elements[3][2]};
            float const denom=direction.x*direction.x+
                              direction.y*direction.y+direction.z*direction.z;
            if(denom<0.0001f)return;
            for(std::size_t i=0;i<result.joints.size();++i) {
                if(!descendant(static_cast<std::uint32_t>(i),a->second) ||
                   !descendant(static_cast<std::uint32_t>(i),root_helper->second) ||
                   mirrored(static_cast<std::uint32_t>(i)))
                    continue;
                // Most-specific segment wins: a child elbow->wrist constraint
                // must not be replaced by shoulder->elbow.
                if(std::any_of(result.secondary_palette_constraints.begin(),
                               result.secondary_palette_constraints.end(),
                    [&](auto const& constraint){return constraint.bone==i;}))
                    continue;
                auto const& bone=globals[i];
                math::float3 delta{
                    bone.elements[3][0]-start.elements[3][0],
                    bone.elements[3][1]-start.elements[3][1],
                    bone.elements[3][2]-start.elements[3][2]};
                float const factor=std::clamp(
                    (delta.x*direction.x+delta.y*direction.y+
                     delta.z*direction.z)/denom,0.f,1.f);
                result.secondary_palette_constraints.push_back({
                    static_cast<std::uint32_t>(i),b->second,c->second,factor});
            }
        };
        // Legs and shoes also contain roll/twist branches that otherwise
        // remain static while the original knee/ankle/ball chains animate.
        // More specific ankle chains are registered before the parent knee
        // and upper-leg segments, retaining a continuous LBS palette.
        segment("l_ankle_helper","l_ankle","l_ball");
        segment("r_ankle_helper","r_ankle","r_ball");
        segment("l_knee_helper","l_knee","l_ankle");
        segment("r_knee_helper","r_knee","r_ankle");
        segment("l_upper_leg_helper","l_upper_leg","l_knee");
        segment("r_upper_leg_helper","r_upper_leg","r_knee");
        // Closest joints first: descend the arm without losing elbow motion.
        segment("l_elbow_helper","l_elbow","l_wrist");
        segment("r_elbow_helper","r_elbow","r_wrist");
        segment("l_shoulder_helper","l_shoulder","l_elbow");
        segment("r_shoulder_helper","r_shoulder","r_elbow");
        auto ring=by_name.find("neck_runtime_grp"),head=by_name.find("headb");
        if(ring!=by_name.end() && head!=by_name.end() &&
           descendant(ring->second,root_helper->second)) {
            for(std::size_t i=0;i<result.joints.size();++i)
                if(descendant(static_cast<std::uint32_t>(i),ring->second) &&
                   !mirrored(static_cast<std::uint32_t>(i))) {
                    auto const it=std::find_if(
                        result.secondary_palette_constraints.begin(),
                        result.secondary_palette_constraints.end(),
                        [&](auto const& entry){return entry.bone==i;});
                    if(it==result.secondary_palette_constraints.end())
                        result.secondary_palette_constraints.push_back({
                            static_cast<std::uint32_t>(i),head->second,head->second,0.f});
                }
        }
    }
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
        // Native weapon idle clips may legitimately contain one keyed
        // pose with a one-frame duration. Do not force >=2 frames.
        auto const expected_duration=clip.frames==1 ?
            1.f/clip.sample_rate : (clip.frames-1)/clip.sample_rate;
        require(tracks > 0 && tracks <= skeleton.joints.size() && clip.frames >= 1 &&
            clip.frames <= 12000 && tracks * clip.frames <= 1500000 &&
            clip.sample_rate >= 1.f && clip.sample_rate <= 240.f && loop <= 1 &&
            std::abs(duration - expected_duration) < 0.02f &&
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
            clip.samples.push_back(transform(f(0),f(1),f(2),f(3),f(4),f(5),f(6),f(7),f(8),f(9),true));
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
    float const frame = last<=0.f ? 0.f :
        (clip.loop ? std::fmod(seconds * clip.sample_rate,last)
                   : std::min(seconds * clip.sample_rate,last));
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
    // The clip keys only primary joints. Bind-equivalent helper branches
    // must follow primary joints or adjacent skinned vertices tear.
    // Import-time validation caches the mapping once; frame sampling does
    // not allocate name maps or repeatedly compare bind quaternions.
    for(auto const& [helper,source]:skeleton.mirrored_helpers) {
        if(std::find(clip.joints.begin(),clip.joints.end(),source)!=clip.joints.end())
            local_[helper]=local_[source];
    }
    evaluate(skeleton);
    // GPU and CPU use the same final deformed palette. Interpolate only
    // constrained secondary joints; primary YCD-driven bones are untouched.
    // Source indices always refer to the primary, already-evaluated rig.
    for(auto const& rule:skeleton.secondary_palette_constraints) {
        if(std::find(clip.joints.begin(),clip.joints.end(),rule.bone)!=clip.joints.end())
            continue;
        auto const& a=palette_[rule.source_a];
        auto const& b=palette_[rule.source_b];
        auto& result=palette_[rule.bone];
        for(int row=0;row<4;++row)
            for(int col=0;col<4;++col)
                result.elements[row][col]=
                    a.elements[row][col]*(1.f-rule.source_b_weight)+
                    b.elements[row][col]*rule.source_b_weight;
    }
}
}
