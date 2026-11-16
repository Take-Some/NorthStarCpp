#include "viewport_skeletal_animation.hpp"
#include "../shared/frame_memory_counters.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace northstarcpp::viewport_detail {
namespace {
constexpr auto missing = static_cast<std::size_t>(-1);

auto find_joint(Skeleton const& skeleton, std::string_view name)
{
    return std::find_if(skeleton.joints.begin(),skeleton.joints.end(),
        [&](auto const& joint) { return joint.name==name; });
}
}

void SkeletalSceneFrame::prepare(Scene const& scene)
{
    animations.clear();
    attachments.clear();
    requests_.clear();
    sockets_.clear();
    indices_.clear();
    order.clear();
    has_cycles=false;
    for(auto const& component:scene.components) {
        if(component.version!=1 || !component.entity_id)continue;
        if(component.type=="engine.model.animation" &&
           !component.payload.empty() && component.payload.size()<=512)
            animations[component.entity_id]=component.payload;
        if(component.type!="engine.model.bone_attachment" || component.payload.size()>240)
            continue;
        BoneAttachment attachment;
        std::istringstream stream(component.payload);
        if(!(stream>>attachment.parent>>attachment.bone>>
             attachment.offset.x>>attachment.offset.y>>attachment.offset.z>>
             attachment.rotation.x>>attachment.rotation.y>>attachment.rotation.z) ||
           !attachment.parent || attachment.bone.empty() || attachment.bone.size()>96)
            continue;
        std::string alignment,extra;
        if(stream>>alignment) {
            if(alignment=="camera_forward")attachment.camera_forward=true;
            else if(alignment!="socket")continue;
            if(stream>>extra)continue;
        }
        auto finite=[](float v){return std::isfinite(v)&&std::abs(v)<=360.f;};
        if(!finite(attachment.offset.x)||!finite(attachment.offset.y)||!finite(attachment.offset.z)||
           !finite(attachment.rotation.x)||!finite(attachment.rotation.y)||!finite(attachment.rotation.z))
            continue;
        auto& bones=requests_[attachment.parent];
        if(std::find(bones.begin(),bones.end(),attachment.bone)==bones.end())
            bones.push_back(attachment.bone);
        attachments.emplace(component.entity_id,std::move(attachment));
    }
    if(attachments.empty()) {
        diagnostics::frame_memory::resize(order,scene.objects.size());
        std::iota(order.begin(),order.end(),std::size_t{0});
        return;
    }
    for(std::size_t i=0;i<scene.objects.size();++i)
        indices_.emplace(scene.objects[i].node.id,i);
    diagnostics::frame_memory::track_capacity(links_,[&] {
        links_.assign(scene.objects.size(),{missing,missing,missing,false});
    });
    for(auto const& [child,attachment]:attachments) {
        auto parent=indices_.find(attachment.parent);
        auto dependent=indices_.find(child);
        if(parent==indices_.end() || dependent==indices_.end() ||
           parent->second==dependent->second)continue;
        auto& chain=links_[parent->second];
        if(chain.first==missing)chain.first=dependent->second;
        else links_[chain.last].next=dependent->second;
        chain.last=dependent->second;
        links_[dependent->second].pending=true;
    }
    diagnostics::frame_memory::reserve(order,scene.objects.size());
    for(std::size_t i=0;i<links_.size();++i)
        if(!links_[i].pending)order.push_back(i);
    for(std::size_t head=0;head<order.size();++head)
        for(auto child=links_[order[head]].first;child!=missing;child=links_[child].next) {
            links_[child].pending=false;
            order.push_back(child);
        }
    has_cycles=order.size()!=scene.objects.size();
    if(has_cycles)
        for(std::size_t i=0;i<links_.size();++i)
            if(links_[i].pending)order.push_back(i);
}

bool SkeletalSceneFrame::wants_hand_grip(std::uint64_t id) const
{
    const auto request=requests_.find(id);
    return request!=requests_.end() &&
        std::find(request->second.begin(),request->second.end(),
                  "r_hand_prop_attachment")!=request->second.end();
}

GripResults evaluate_pose(PoseInput const& input, SkeletonPose& output)
{
    output.sample(input.skeleton,input.clip,input.seconds,false);
    GripResults results{};
    if(!input.grip)return results;

    math::float4x4 world_to_mesh{},skeleton_to_mesh{};
    if(!world_to_mesh.try_invert(input.mesh_world) ||
       !skeleton_to_mesh.try_invert(input.mesh_to_skeleton))
        throw std::runtime_error("Non-invertible equipped pose");
    auto const world_to_skeleton=world_to_mesh*input.mesh_to_skeleton;
    const float yaw=input.grip->yaw;
    const float pitch=std::clamp(input.grip->pitch,-1.3f,1.3f);
    const Vector3 forward{
        std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    const Vector3 right{std::cos(yaw),0.f,-std::sin(yaw)};
    // Camera-space vertical is perpendicular to the actual aim direction.
    // World Y below caused wrist and elbow goals to diverge when looking
    // up or down, pulling the two arms apart in first-person mode.
    const Vector3 up{-std::sin(yaw)*std::sin(pitch),
        std::cos(pitch),-std::cos(yaw)*std::sin(pitch)};
    auto joint_world=[&](char const* name) -> std::optional<Vector3> {
        auto const joint=find_joint(input.skeleton,name);
        if(joint==input.skeleton.joints.end())return std::nullopt;
        auto const index=static_cast<std::size_t>(joint-input.skeleton.joints.begin());
        return (output.joints_world().at(index)*skeleton_to_mesh*input.mesh_world)
            .transform_position({0.f,0.f,0.f});
    };
    auto aim=[&](std::size_t hand,char const* shoulder,char const* elbow,
                 char const* wrist,Vector3 goal,Vector3 pole) {
        const bool fps=input.grip->first_person;
        const auto target=world_to_skeleton.transform_position(goal);
        const auto elbow_pole=world_to_skeleton.transform_position(pole);
        bool solved=false;
        // A single bounded two-bone pass can leave a residual on asymmetric
        // authored bind poses. Refine only while convergence improves, and
        // stop early to preserve the realtime animation budget.
        float error=std::numeric_limits<float>::infinity();
        for(unsigned iteration=0;iteration<(fps?3u:1u);++iteration) {
            const bool moved=output.pose_two_bone(input.skeleton,shoulder,
                elbow,wrist,target,elbow_pole,
                fps?1.55f:1.30f,fps?1.80f:1.55f);
            if(!moved)break;
            solved=true;
            auto const actual=joint_world(wrist);
            if(!actual)break;
            const float next=(goal-*actual).magnitude();
            if(next<.006f || next>=error-.0002f)break;
            error=next;
        }
        if(auto const actual=joint_world(wrist))
            results[hand]=GripResult{goal,*actual,solved};
    };
    // WeaponLab reference: a camera-relative weapon anchor drives BOTH
    // grip targets. Never place the support hand relative to the right hand's
    // solved wrist, because independent IK error would accumulate.
    bool used_camera_grip=false;
    if(input.grip->first_person && input.grip->eye &&
       input.grip->eye->valid()) {
        auto const eye=*input.grip->eye;
        auto const shoulder_right=joint_world("r_shoulder");
        auto const elbow_right=joint_world("r_elbow");
        auto const wrist_right=joint_world("r_wrist");
        auto const shoulder_left=joint_world("l_shoulder");
        auto const elbow_left=joint_world("l_elbow");
        auto const wrist_left=joint_world("l_wrist");
        // Ignore FPS camera poses that are not physically attached to the rig.
        if(shoulder_right && elbow_right && wrist_right &&
           shoulder_left && elbow_left && wrist_left &&
           (eye-(*shoulder_right+*shoulder_left)*.5f).magnitude()<1.6f) {
            auto const dot=[](Vector3 a,Vector3 b) {
                return a.x*b.x+a.y*b.y+a.z*b.z;
            };
            // Calibrated WeaponLab pistol offsets: independent wrist joints
            // around the same firearm grip/support, not arbitrary point gaps.
            constexpr float gun_drop=.34f;
            const Vector3 right_offset=right*(-.032f)-up*.008f-forward*.068f;
            const Vector3 left_offset=right*(.028f+.030f)-
                up*(.018f+.020f)+forward*(-.054f+.020f);
            auto const base=eye-up*gun_drop;
            auto const radii=std::array<float,2>{
                (*shoulder_right-*elbow_right).magnitude()+
                    (*elbow_right-*wrist_right).magnitude()-.012f,
                (*shoulder_left-*elbow_left).magnitude()+
                    (*elbow_left-*wrist_left).magnitude()-.012f};
            auto const shoulders=std::array<Vector3,2>{
                *shoulder_right,*shoulder_left};
            auto const offsets=std::array<Vector3,2>{
                right_offset,left_offset};
            float min_depth=.12f,max_depth=2.f;
            for(std::size_t i=0;i<2;++i) {
                const Vector3 delta=base+offsets[i]-shoulders[i];
                const float along=dot(delta,forward);
                const float radius=std::max(.1f,radii[i]);
                const float discriminant=radius*radius-dot(delta,delta)+along*along;
                if(discriminant<0.f)continue;
                const float span=std::sqrt(discriminant);
                min_depth=std::max(min_depth,-along-span);
                max_depth=std::min(max_depth,-along+span);
            }
            if(min_depth<=max_depth) {
                const float depth=std::clamp(.43f,min_depth,max_depth);
                const Vector3 anchor=base+forward*depth;
                const Vector3 pole_right=*shoulder_right+right*.20f-
                    forward*.04f-up*.27f;
                const Vector3 pole_left=*shoulder_left-right*.20f-
                    forward*.04f-up*.27f;
                aim(0,"r_shoulder","r_elbow","r_wrist",
                    anchor+right_offset,pole_right);
                // Unlike the old right-wrist approximation, the handgun
                // anchor is the REAL animated r_hand_prop_attachment
                // socket. The NewViso WeaponLab pistol's support/grip
                // offsets are expressed relative to that same firearm.
                auto const socket=joint_world("r_hand_prop_attachment");
                const Vector3 support_goal=socket?
                    *socket+right*.090f-up*.030f-forward*.009f:
                    anchor+left_offset;
                aim(1,"l_shoulder","l_elbow","l_wrist",
                    support_goal,pole_left);
                used_camera_grip=true;
            }
        }
    }
    if(!used_camera_grip)
    if(auto const shoulder=joint_world("r_shoulder")) {
        const bool fps=input.grip->first_person;
        // The actual pistol grip is attached to the right hand's animated
        // prop socket. Place the support hand immediately beside that grip,
        // not 13 cm away. Explicit elbow poles keep both elbows down/out.
        const Vector3 target=*shoulder+forward*(fps?.30f:.37f)-
            right*(fps?.075f:.065f)-up*(fps?.16f:.15f);
        // The elbow pole is below and slightly behind the aiming hands,
        // keeping the forearms coming up from the lower corners of the view.
        const Vector3 elbow_pole=*shoulder+right*.18f-forward*.08f-
            up*(fps?.43f:.24f);
        aim(0,"r_shoulder","r_elbow","r_wrist",target,elbow_pole);
        if(auto const wrist=joint_world("r_wrist"))
            if(auto const left=joint_world("l_shoulder")) {
                const Vector3 support=*wrist-right*.055f+forward*.015f-
                    up*.012f;
                const Vector3 support_pole=*left-right*.16f-forward*.08f-
                    up*(fps?.43f:.24f);
                aim(1,"l_shoulder","l_elbow","l_wrist",
                    support,support_pole);
            }
    }
    // Abby's 1033-joint rig has additional skin-weighted finger-roll branches.
    // Never apply a procedural primary-finger curl without driving all of
    // those branches: it stretches skinned hand triangles by >100x.
    // Preserve the validated animated finger pose until a complete imported
    // finger/roll retarget is available.
    return results;
}

void SkeletalSceneFrame::publish_sockets(std::uint64_t id, Skeleton const& skeleton,
    SkeletonPose const& evaluated, math::float4x4 const& mesh_to_skeleton,
    math::float4x4 const& mesh_world)
{
    const auto request=requests_.find(id);
    if(request==requests_.end())return;
    math::float4x4 skeleton_to_mesh;
    if(!skeleton_to_mesh.try_invert(mesh_to_skeleton))
        throw std::runtime_error("Non-invertible skeletal mesh basis");
    auto& joints=sockets_[id];
    for(auto const& bone:request->second) {
        auto const joint=find_joint(skeleton,bone);
        if(joint==skeleton.joints.end())continue;
        auto const index=static_cast<std::size_t>(joint-skeleton.joints.begin());
        joints.emplace(bone,(evaluated.joints_world().at(index)*skeleton_to_mesh)*mesh_world);
    }
}

std::optional<math::float4x4> SkeletalSceneFrame::attachment_transform(
    BoneAttachment const& attachment, CameraPose const* camera) const
{
    auto const origin=sockets_.find(attachment.parent);
    if(origin==sockets_.end())return {};
    auto const joint=origin->second.find(attachment.bone);
    if(joint==origin->second.end())return {};
    constexpr float radians=0.01745329251994329577f;
    auto const rotation=math::create_rotation_x(attachment.rotation.x*radians) *
        math::create_rotation_y(attachment.rotation.y*radians) *
        math::create_rotation_z(attachment.rotation.z*radians);
    auto transform=(rotation*math::create_translation(attachment.offset))*joint->second;
    if(attachment.camera_forward && camera) {
        const float sy=std::sin(camera->yaw),cy=std::cos(camera->yaw);
        const float sp=std::sin(camera->pitch),cp=std::cos(camera->pitch);
        const float basis[3][3]{{cy,0.f,-sy},{-sy*sp,cp,-cy*sp},{sy*cp,sp,cy*cp}};
        for(unsigned row=0;row<3;++row)
            for(unsigned col=0;col<3;++col)
                transform.elements[row][col]=rotation.elements[row][0]*basis[0][col]+
                    rotation.elements[row][1]*basis[1][col]+rotation.elements[row][2]*basis[2][col];
    }
    return transform;
}
} // namespace northstarcpp::viewport_detail
