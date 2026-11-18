#include <northstarcpp/skeleton.hpp>
#include <northstarcpp/assets.hpp>
#include "asset_list_file.hpp"
#include <boost/property_tree/xml_parser.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>
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

std::size_t Skeleton::joint_index(std::string_view name) const noexcept
{
    if(joint_name_order.size()==joints.size()) {
        auto found=std::lower_bound(joint_name_order.begin(),
            joint_name_order.end(),name,
            [&](std::uint32_t index,std::string_view sought) {
                return std::string_view(joints[index].name)<sought;
            });
        if(found!=joint_name_order.end() &&
           std::string_view(joints[*found].name)==name)
            return *found;
        return joints.size();
    }
    // Test fixtures and manually created rigs may not have an import index.
    for(std::size_t i=0;i<joints.size();++i)
        if(std::string_view(joints[i].name)==name)return i;
    return joints.size();
}

CanonicalHumanoidSkeleton detect_canonical_humanoid_skeleton(Skeleton const& rig)
{
    using H=CanonicalHumanoidSkeleton;
    H map;
    auto find=[&](std::initializer_list<std::string_view> aliases) {
        for(auto name:aliases) {
            const auto index=rig.joint_index(name);
            if(index<rig.joints.size())return index;
        }
        return H::missing;
    };
    map.pelvis=find({"pelvis","hips"});
    map.spine=find({"spineb","spine","spine_01"});
    map.chest=find({"spined","chest","spine_03"});
    map.neck=find({"neck","neckb","neck_01"});
    map.head=find({"headb","head"});
    map.r_clavicle=find({"r_clavicle","r_collar","clavicle_r"});
    map.l_clavicle=find({"l_clavicle","l_collar","clavicle_l"});
    map.r_upperarm=find({"r_shoulder","r_upperarm","upperarm_r"});
    map.l_upperarm=find({"l_shoulder","l_upperarm","upperarm_l"});
    map.r_forearm=find({"r_elbow","r_forearm","lowerarm_r"});
    map.l_forearm=find({"l_elbow","l_forearm","lowerarm_l"});
    map.r_hand=find({"r_wrist","r_hand","hand_r"});
    map.l_hand=find({"l_wrist","l_hand","hand_l"});
    map.r_hand_prop=find({"r_hand_prop_attachment","r_weapon_socket"});
    map.l_hand_prop=find({"l_hand_prop_attachment","l_weapon_socket"});
    auto descends=[&](std::size_t child,std::size_t ancestor) {
        if(child==H::missing || ancestor==H::missing)return false;
        std::size_t limit=rig.joints.size();
        for(int i=static_cast<int>(child);i>=0 && limit--!=0;
            i=rig.joints[static_cast<std::size_t>(i)].parent)
            if(static_cast<std::size_t>(i)==ancestor)return true;
        return false;
    };
    auto chain=[&](std::size_t a,std::size_t b,std::size_t c) {
        return descends(b,a) && descends(c,b);
    };
    map.valid=chain(map.r_upperarm,map.r_forearm,map.r_hand) &&
              chain(map.l_upperarm,map.l_forearm,map.l_hand) &&
              map.r_upperarm!=map.l_upperarm && map.r_hand!=map.l_hand;
    if(!map.valid)return map;
    if(map.r_hand_prop!=H::missing &&
       !descends(map.r_hand_prop,map.r_hand))map.r_hand_prop=H::missing;
    if(map.l_hand_prop!=H::missing &&
       !descends(map.l_hand_prop,map.l_hand))map.l_hand_prop=H::missing;
    // Bind transforms are hierarchical; X alone does not establish a side.
    // Establish front from the eye/head and up from pelvis/head, then derive
    // anatomical right. If either axis is ambiguous, DO NOT swap names.
    std::vector<math::float4x4> bind_world;
    bind_world.reserve(rig.joints.size());
    for(std::size_t i=0;i<rig.joints.size();++i) {
        auto const& joint=rig.joints[i];
        auto transform=math::create_scale(joint.bind.scale)*
            math::create_matrix(joint.bind.rotation,joint.bind.position);
        if(joint.parent>=0) {
            if(static_cast<std::size_t>(joint.parent)>=i)return map;
            transform=transform*bind_world[static_cast<std::size_t>(joint.parent)];
        }
        bind_world.push_back(transform);
    }
    auto position=[&](std::size_t index)->math::float3 {
        return bind_world[index].transform_position({0.f,0.f,0.f});
    };
    if(map.pelvis!=H::missing && map.head!=H::missing) {
        // A single right-eye position is laterally offset from the head:
        // interpreting it as "forward" can flip a perfectly valid arm map.
        // Require a bilateral eye pair to establish the face's midline.
        auto const right_eye=find({"r_eyeball","eye_r"});
        auto const left_eye=find({"l_eyeball","eye_l"});
        if(right_eye!=H::missing && left_eye!=H::missing &&
           descends(right_eye,map.head) && descends(left_eye,map.head)) {
            auto up=position(map.head)-position(map.pelvis);
            auto forward=(position(right_eye)+position(left_eye))*.5f-
                         position(map.head);
            if(up.magnitude()>.25f && forward.magnitude()>.04f) {
                up.normalize();
                forward=forward-up*(forward.x*up.x+forward.y*up.y+
                                   forward.z*up.z);
                if(forward.magnitude()>.04f) {
                    forward.normalize();
                    // Model-space anatomical right follows the engine's
                    // up/forward convention, not the eye bones' names: a
                    // source YMT can call its physically left eye "right".
                    // The bilateral eye midpoint still avoids the lateral
                    // bias of measuring forward from a single eye.
                    auto right=math::cross_product(up,forward);
                    if(right.magnitude()<.5f)return map;
                    right.normalize();
                    auto const lateral=position(map.r_upperarm)-
                                       position(map.l_upperarm);
                    float const separation=lateral.x*right.x+
                        lateral.y*right.y+lateral.z*right.z;
                    if(std::abs(separation)>.12f) {
                        map.side_confirmed=true;
                        if(separation<0.f) {
                            std::swap(map.r_clavicle,map.l_clavicle);
                            std::swap(map.r_upperarm,map.l_upperarm);
                            std::swap(map.r_forearm,map.l_forearm);
                            std::swap(map.r_hand,map.l_hand);
                            std::swap(map.r_hand_prop,map.l_hand_prop);
                            map.side_repaired=true;
                        }
                    }
                }
            }
        }
    }
    // Build a wrist-local finger/thumb basis, usable by orientation retarget.
    // We require a middle-finger base AND thumb base on the same hand chain.
    for(unsigned hand=0;hand<2;++hand) {
        const auto wrist=hand==0?map.r_hand:map.l_hand;
        auto const& wrist_name=rig.joints[wrist].name;
        const bool is_right=wrist_name.compare(0,2,"r_")==0 ||
            (wrist_name.size()>=2 &&
             wrist_name.compare(wrist_name.size()-2,2,"_r")==0);
        const auto middle=is_right?
            find({"r_middlea","r_middle_01"}):
            find({"l_middlea","l_middle_01"});
        const auto thumb=is_right?
            find({"r_thumba","r_thumb_01"}):
            find({"l_thumba","l_thumb_01"});
        if(!descends(middle,wrist)||!descends(thumb,wrist))continue;
        math::float4x4 inverse;
        if(!inverse.try_invert(bind_world[wrist]))continue;
        auto finger=(bind_world[middle]*inverse).transform_position({0,0,0});
        auto lateral=(bind_world[thumb]*inverse).transform_position({0,0,0});
        if(finger.magnitude()<.012f || lateral.magnitude()<.009f)continue;
        finger.normalize();
        lateral=lateral-finger*(finger.x*lateral.x+
                                 finger.y*lateral.y+finger.z*lateral.z);
        if(lateral.magnitude()<.006f)continue;
        lateral.normalize();
        map.finger_axis[hand]=finger;
        map.thumb_axis[hand]=lateral;
        map.hand_basis_valid[hand]=true;
    }
    return map;
}

Skeleton load_skeleton_from_bytes(std::vector<std::uint8_t> const& nef8)
{
    using namespace shared_assets::detail;
    auto file = parse_list_file(nef8);
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
        // Prefer precise finger helper segments over generic wrist/elbow
        // bindings, so each skin-weighted helper follows the animated digit.
        for(std::string_view side : {"l","r"}) {
            auto name=[&](std::string_view tail){
                return std::string(side)+"_"+std::string(tail);
            };
            segment(name("finger_roll_13"),name("thumbb"),name("thumbc"));
            segment(name("finger_roll_12"),name("thumba"),name("thumbb"));
            segment(name("thumbb_helper"),name("thumba"),name("thumbb"));
            segment(name("finger_roll_14"),name("wrist"),name("thumba"));
            segment(name("thumba_helper"),name("wrist"),name("thumba"));
            for(std::string_view digit : {"index","middle","ring","pinky"}) {
                auto const base=std::string(digit);
                const int roll=digit=="index"?0:digit=="middle"?3:
                    digit=="ring"?6:9;
                auto roll_name=[&](int offset){
                    int const value=roll+offset;
                    return name("finger_roll_"+
                        std::string(value<10?"0":"")+std::to_string(value));
                };
                segment(roll_name(2),name(base+"b"),name(base+"c"));
                segment(roll_name(1),name(base+"a"),name(base+"b"));
                segment(name(base+"b_helper"),name(base+"a"),name(base+"b"));
                segment(roll_name(0),name(base+"_meta"),name(base+"a"));
                segment(name(base+"a_helper"),name(base+"_meta"),name(base+"a"));
                segment(name(base+"_meta_helper"),name("wrist"),name(base+"_meta"));
            }
        }
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
    result.joint_name_order.resize(result.joints.size());
    std::iota(result.joint_name_order.begin(),result.joint_name_order.end(),0u);
    std::sort(result.joint_name_order.begin(),result.joint_name_order.end(),
        [&](std::uint32_t a,std::uint32_t b) {
            return result.joints[a].name<result.joints[b].name;
        });
    result.humanoid=detect_canonical_humanoid_skeleton(result);
    return result;
}
Skeleton load_skeleton(AssetStore const& assets,std::string const& path)
{
    if(assets.size(path)>64u*1024u*1024u)
        throw std::runtime_error("YMT source exceeds 64 MiB");
    return load_skeleton_from_bytes(assets.read(path));
}

std::vector<SkeletalClip> load_skeletal_clips_from_bytes(
    std::vector<std::uint8_t> const& nef8,
    std::vector<std::string> const& names, Skeleton const& skeleton)
{
    using namespace shared_assets::detail;
    auto file = parse_list_file(nef8);
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
    std::unordered_set<std::string> requested(names.begin(),names.end());
    require(requested.size()==names.size(),"Duplicate requested clip name");
    std::vector<SkeletalClip> result;
    result.reserve(requested.size());
    for (std::size_t entry = 0; entry < file.entries; ++entry) {
        auto const record = table + 64 * entry;
        auto name = name_at(bytes, strings, payload, bytes.integer(record + 8,4));
        if (requested.find(name)==requested.end()) continue;
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

std::vector<SkeletalClip> load_skeletal_clips(AssetStore const& assets,
    std::string const& path,std::vector<std::string> const& names,
    Skeleton const& skeleton) {
    if(assets.size(path)>64ull*1024ull*1024ull)
        throw std::invalid_argument("YCD dictionary exceeds 64 MiB");
    return load_skeletal_clips_from_bytes(assets.read(path),names,skeleton);
}

SkeletonPose::SkeletonPose(Skeleton const& skeleton)
    : global_(skeleton.joints.size()), palette_(skeleton.joints.size()),
      keyed_(skeleton.joints.size(),0)
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
    // Even manually authored clips must be bounds-safe, not only YCD imports.
    require(clip.frames>=1 && clip.frames<=12000 &&
        std::isfinite(clip.sample_rate) && clip.sample_rate>0.f &&
        local_.size()==skeleton.joints.size() &&
        clip.joints.size()<=local_.size() &&
        clip.samples.size()==clip.joints.size()*clip.frames,
        "Invalid skeletal clip buffers");
    float const last = static_cast<float>(clip.frames - 1);
    // Keep elapsed time in double before modulo to avoid overflow.
    double const ticks=static_cast<double>(seconds)*clip.sample_rate;
    float const frame = last<=0.f ? 0.f :
        (clip.loop ? std::fmod(ticks,static_cast<double>(last))
                   : std::min(ticks,static_cast<double>(last)));
    auto const first = static_cast<std::size_t>(frame);
    auto const next = std::min(first + 1, static_cast<std::size_t>(clip.frames - 1));
    auto const fraction = frame - static_cast<float>(first);
    for (std::size_t i = 0; i < local_.size(); ++i) local_[i] = skeleton.joints[i].bind;
    std::fill(keyed_.begin(),keyed_.end(),std::uint8_t{0});
    for(auto const joint:clip.joints) {
        require(joint<keyed_.size(),"Animation track references missing joint");
        keyed_[joint]=1;
    }
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
        if(keyed_[source])
            local_[helper]=local_[source];
    }
    evaluate(skeleton);
    // GPU and CPU use the same final deformed palette. Interpolate only
    // constrained secondary joints; primary YCD-driven bones are untouched.
    // Source indices always refer to the primary, already-evaluated rig.
    for(auto const& rule:skeleton.secondary_palette_constraints) {
        if(keyed_[rule.bone])
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

void SkeletonPose::preserve_unkeyed_and_blend(
    Skeleton const& rig,
    std::vector<JointTransform> const& previous,
    std::vector<JointTransform> const& outgoing,float alpha)
{
    if(previous.size()!=local_.size() ||
       !std::isfinite(alpha)||alpha<0.f||alpha>1.f)
        throw std::invalid_argument("Invalid continuous skeletal sample");
    bool const crossfade=alpha<1.f && outgoing.size()==local_.size();
    // sample() only keys a subset of YCD tracks. Replacing every missing
    // joint with its YMT bind transform exposes an A/T pose during turns,
    // falls and transitions. Keep the last *pre-IK* animated local pose.
    for(std::size_t i=0;i<local_.size();++i) {
        if(!keyed_[i]) {
            local_[i]=previous[i];
            continue;
        }
        if(crossfade) {
            auto const& from=outgoing[i];
            auto& to=local_[i];
            to.position=from.position+(to.position-from.position)*alpha;
            to.rotation=math::slerp(from.rotation,to.rotation,alpha);
            to.scale=from.scale+(to.scale-from.scale)*alpha;
        }
    }
    // Re-apply the imported helper/secondary palette constraints exactly
    // as in normal sampling. No temporary allocations or skeleton copies.
    set_local_transforms(rig,local_,keyed_);
}

void SkeletonPose::set_local_transforms(Skeleton const& rig,
    std::vector<JointTransform> const& locals,
    std::vector<std::uint8_t> const& keyed) {
    using namespace shared_assets::detail;
    require(locals.size()==local_.size() && keyed.size()==local_.size() &&
            rig.joints.size()==local_.size(),
            "Animation Graph local pose does not match skeleton");
    // preserve_unkeyed_and_blend operates on this same local_ buffer.
    // Copy only when the graph supplies a distinct transform array.
    if(locals.data()!=local_.data())
        std::copy(locals.begin(),locals.end(),local_.begin());
    for(auto const& pair:rig.mirrored_helpers)
        if(pair.first<local_.size() && pair.second<local_.size() &&
           keyed[pair.second])local_[pair.first]=local_[pair.second];
    evaluate(rig);
    for(auto const& rule:rig.secondary_palette_constraints) {
        if(rule.bone>=palette_.size() || rule.source_a>=palette_.size() ||
           rule.source_b>=palette_.size() || keyed[rule.bone])continue;
        auto const& a=palette_[rule.source_a];
        auto const& b=palette_[rule.source_b];
        auto& output=palette_[rule.bone];
        for(unsigned row=0;row<4;++row)
            for(unsigned col=0;col<4;++col)
                output.elements[row][col]=
                    a.elements[row][col]*(1.f-rule.source_b_weight)+
                    b.elements[row][col]*rule.source_b_weight;
    }
}

bool SkeletonPose::apply_finger_bends(Skeleton const& rig,
    std::vector<FingerJointBend> const& bends,float weight)
{
    if(bends.empty() || bends.size()>64 || !std::isfinite(weight) ||
       weight<=0.f || weight>1.f || local_.size()!=rig.joints.size())
        return false;
    // Transactional validation: a partial rig mapping makes the hands look
    // worse than the source clip. Never apply only some of a grip profile.
    for(auto const& bend:bends) {
        if(bend.joint.empty() || bend.joint.size()>96 ||
           rig.joint_index(bend.joint)>=local_.size() ||
           !bend.degrees.valid() ||
           std::abs(bend.degrees.x)>90.f ||
           std::abs(bend.degrees.y)>90.f ||
           std::abs(bend.degrees.z)>90.f)
            return false;
    }
    constexpr float radians=0.01745329251994329577f;
    for(auto const& bend:bends) {
        auto const id=rig.joint_index(bend.joint);
        auto const angle=bend.degrees*weight*radians;
        auto const matrix=math::create_rotation_x(angle.x)*
            math::create_rotation_y(angle.y)*
            math::create_rotation_z(angle.z);
        auto const delta=math::quaternion(matrix).normalize();
        local_[id].rotation=(local_[id].rotation*delta).normalize();
        // Same-bind primary/helper pairs form parallel trees; keeping both
        // local TRS equal is required when skin weights reference the helper.
        for(auto const& [helper,source]:rig.mirrored_helpers)
            if(source==id && helper<local_.size())
                local_[helper].rotation=local_[id].rotation;
    }
    evaluate(rig);
    // Finger helper/roll skinning is computed from the final primary
    // phalanx palette. These constraints are import-time cached, not
    // dynamically discovered in each animation frame.
    for(auto const& rule:rig.secondary_palette_constraints) {
        if(rule.bone>=palette_.size() || rule.source_a>=palette_.size() ||
           rule.source_b>=palette_.size())continue;
        auto const& a=palette_[rule.source_a];
        auto const& b=palette_[rule.source_b];
        auto& output=palette_[rule.bone];
        for(unsigned row=0;row<4;++row)
            for(unsigned col=0;col<4;++col)
                output.elements[row][col]=
                    a.elements[row][col]*(1.f-rule.source_b_weight)+
                    b.elements[row][col]*rule.source_b_weight;
    }
    return true;
}

std::optional<math::float3> SkeletonPose::consume_visual_root_motion(
    Skeleton const& rig,std::string_view root) {
    auto const id=rig.joint_index(root);
    if(id>=local_.size()||id>=rig.joints.size()||
       rig.joints[id].parent!=-1)return std::nullopt;
    auto const delta=local_[id].position-rig.joints[id].bind.position;
    if(!delta.valid())return std::nullopt;
    local_[id].position=rig.joints[id].bind.position;
    // Preserve the same imported secondary/helper palette rules used by
    // the base sampler and Animation Graph evaluator.
    set_local_transforms(rig,local_,keyed_);
    return delta;
}

bool SkeletonPose::adjust_local_joint(Skeleton const& rig,
    std::string_view name,math::float3 offset,math::float3 axis,float radians) {
    if(!offset.valid()||!axis.valid()||!std::isfinite(radians)||
       std::abs(radians)>.2f||offset.magnitude()>.3f ||
       rig.joints.size()!=local_.size())return false;
    auto const id=rig.joint_index(name);
    if(id>=local_.size())return false;
    if(std::abs(radians)>1.e-5f &&axis.magnitude()<.0001f)
        return false;
    local_[id].position+=offset;
    if(std::abs(radians)>1.e-5f) {
        math::quaternion rotation(axis.normalize(),radians);
        local_[id].rotation=(local_[id].rotation*rotation).normalize();
    }
    // Imported bind-identical helper branches may skin the clavicle and
    // shoulder alongside the primary chain. Keep them in lockstep when a
    // bounded procedural correction changes the primary joint.
    for(auto const& [helper,source]:rig.mirrored_helpers)
        if(source==id && helper<local_.size())
            local_[helper]=local_[id];
    evaluate(rig);
    for(auto const& rule:rig.secondary_palette_constraints) {
        if(rule.bone>=palette_.size()||rule.source_a>=palette_.size()||
           rule.source_b>=palette_.size())continue;
        auto const& first=palette_[rule.source_a];
        auto const& second=palette_[rule.source_b];
        auto& result=palette_[rule.bone];
        for(int row=0;row<4;++row)
            for(int col=0;col<4;++col)
                result.elements[row][col]=
                    first.elements[row][col]*(1.f-rule.source_b_weight)+
                    second.elements[row][col]*rule.source_b_weight;
    }
    return true;
}

bool SkeletonPose::aim_hand(Skeleton const& skeleton,char const* shoulder_name,
    char const* elbow_name,char const* wrist_name,math::float3 target,
    unsigned iterations) {
    if(!target.valid() || iterations<1 || iterations>12)return false;
    auto index_of=[&](char const* name) -> std::size_t {
        return skeleton.joint_index(name);
    };
    auto shoulder=index_of(shoulder_name);
    auto elbow=index_of(elbow_name);
    auto wrist=index_of(wrist_name);
    if(wrist>=skeleton.joints.size() || elbow>=skeleton.joints.size() ||
       shoulder>=skeleton.joints.size() || wrist==elbow || elbow==shoulder)
        return false;
    // Twist and socket helper joints may intervene in imported arm chains.
    auto descendant=[&](std::size_t child,std::size_t ancestor) {
        for(std::size_t depth=0;depth<skeleton.joints.size();++depth) {
            if(child==ancestor)return true;
            auto const parent=skeleton.joints[child].parent;
            if(parent<0 || static_cast<std::size_t>(parent)>=skeleton.joints.size())
                return false;
            child=static_cast<std::size_t>(parent);
        }
        return false;
    };
    if(!descendant(wrist,elbow) || !descendant(elbow,shoulder))
        return false;

    auto origin=[&](std::size_t joint) {
        auto const& m=global_[joint];
        return math::float3{m.elements[3][0],m.elements[3][1],m.elements[3][2]};
    };
    bool changed=false;
    for(unsigned iter=0;iter<iterations;++iter) {
        for(auto joint:{elbow,shoulder}) {
            auto const pivot=origin(joint);
            auto const current=origin(wrist)-pivot;
            auto const desired=target-pivot;
            auto const a=current.magnitude(),b=desired.magnitude();
            if(a<1.e-5f || b<1.e-5f)continue;
            auto v=math::cross_product(desired,current);
            auto const mag=v.magnitude();
            if(mag<1.e-6f)continue;
            float const cosine=std::clamp(math::dot_product(current,desired)/(a*b),
                                          -1.f,1.f);
            float const radians=std::min(std::atan2(mag,math::dot_product(current,desired)),
                                         .65f);
            if(radians<1.e-4f || cosine>0.999999f)continue;
            auto axis=v/mag;
            auto const parent=skeleton.joints[joint].parent;
            if(parent>=0) {
                math::float4x4 inverse{};
                if(!inverse.try_invert(global_[static_cast<std::size_t>(parent)]))
                    continue;
                axis=inverse.transform_direction(axis).normalize();
            }
            auto delta=math::quaternion(axis,radians);
            // Legacy unconstrained CCD path for generic rigs. Weapon
            // holding uses pole-guided, angular-limited pose_two_bone().
            local_[joint].rotation=(local_[joint].rotation*delta).normalize();
            // Abby and other imported rigs can have bind-identical helper
            // branches used by the visible skin. Keep those in step with
            // their primary arm joints before evaluating the final palette.
            for(auto const& [helper,source]:skeleton.mirrored_helpers)
                if(source==joint) local_[helper].rotation=local_[joint].rotation;
            evaluate(skeleton);
            changed=true;
        }
        if((origin(wrist)-target).magnitude()<.018f)break;
    }
    // The same secondary skin constraints used by normal YCD playback.
    for(auto const& rule:skeleton.secondary_palette_constraints) {
        auto const& a=palette_[rule.source_a];
        auto const& b=palette_[rule.source_b];
        auto& output=palette_[rule.bone];
        for(int row=0;row<4;++row)
            for(int col=0;col<4;++col)
                output.elements[row][col]=
                    a.elements[row][col]*(1.f-rule.source_b_weight)+
                    b.elements[row][col]*rule.source_b_weight;
    }
    return changed;
}

}
