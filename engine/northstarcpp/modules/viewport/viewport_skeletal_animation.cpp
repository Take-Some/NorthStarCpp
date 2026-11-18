#include "viewport_skeletal_animation.hpp"
#include "viewport_character_self_collision.hpp"
#include "viewport_elbow_collision_ik.hpp"
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
    auto const index=skeleton.joint_index(name);
    return index<skeleton.joints.size()?
        skeleton.joints.begin()+static_cast<std::ptrdiff_t>(index):
        skeleton.joints.end();
}
}

void SkeletalSceneFrame::prepare(Scene const& scene)
{
    animations.clear();
    procedural_humanoids.clear();
    graphs.clear();
    graph_parameters.clear();
    ik_chains.clear();
    ik_targets.clear();
    ik_contacts.clear();
    ground_feet.clear();
    ground_bodies.clear();
    look_joints.clear();
    look_targets.clear();
    vehicle_seat_poses.clear();
    vehicle_reaches.clear();
    root_motion_policies.clear();
    attachments.clear();
    requests_.clear();
    sockets_.clear();
    self_collisions_.clear();
    indices_.clear();
    order.clear();
    has_cycles=false;
    for(auto const& component:scene.components) {
        if(component.version!=1 || !component.entity_id)continue;
        if(component.type=="engine.model.procedural_humanoid" &&
           component.payload=="locomotion_v1")
            procedural_humanoids.insert(component.entity_id);
        if(component.type=="engine.model.animation" &&
           !component.payload.empty() && component.payload.size()<=512)
            animations[component.entity_id]=component.payload;
        if(component.type=="engine.model.animation_graph" &&
           !component.payload.empty() && component.payload.size()<=512)
            graphs[component.entity_id]=component.payload;
        if(component.type=="engine.model.animation_parameter" &&
           component.payload.size()<=160) {
            std::istringstream values(component.payload);
            std::string parameter,extra;
            float value=0.f;
            if(values>>parameter>>value && !(values>>extra) &&
               std::isfinite(value) && parameter.size()<=128 &&
               graph_parameters[component.entity_id].size()<32)
                graph_parameters[component.entity_id].emplace_back(
                    std::move(parameter),value);
        }
        if(component.type=="engine.model.ik_chain" &&
           !component.payload.empty() && component.payload.size()<=256 &&
           ik_chains[component.entity_id].size()<16)
            ik_chains[component.entity_id].push_back(component.payload);
        if(component.type=="engine.model.ik_target" &&
           !component.payload.empty() && component.payload.size()<=256 &&
           ik_targets[component.entity_id].size()<16)
            ik_targets[component.entity_id].push_back(component.payload);
        if(component.type=="engine.model.ik_ground_contact" &&
           !component.payload.empty() && component.payload.size()<=256 &&
           ik_contacts[component.entity_id].size()<16)
            ik_contacts[component.entity_id].push_back(component.payload);
        if(component.type=="engine.model.grounded_foot" &&
           !component.payload.empty() &&component.payload.size()<=256 &&
           ground_feet[component.entity_id].size()<4)
            ground_feet[component.entity_id].push_back(component.payload);
        if(component.type=="engine.model.grounded_body" &&
           !component.payload.empty() &&component.payload.size()<=128)
            ground_bodies[component.entity_id]=component.payload;
        if(component.type=="engine.model.look_joint" &&
           !component.payload.empty() &&component.payload.size()<=180 &&
           look_joints[component.entity_id].size()<8)
            look_joints[component.entity_id].push_back(component.payload);
        if(component.type=="engine.model.look_target" &&
           !component.payload.empty() &&component.payload.size()<=120)
            look_targets[component.entity_id]=component.payload;
        if(component.type=="engine.model.vehicle_seat_pose" &&
           !component.payload.empty() &&component.payload.size()<=160)
            vehicle_seat_poses[component.entity_id]=component.payload;
        if(component.type=="engine.model.vehicle_reach" &&
           !component.payload.empty() &&component.payload.size()<=200 &&
           vehicle_reaches[component.entity_id].size()<8)
            vehicle_reaches[component.entity_id].push_back(component.payload);
        if(component.type=="engine.model.root_motion_policy" &&
           !component.payload.empty() &&component.payload.size()<=128)
            root_motion_policies[component.entity_id]=component.payload;
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
    return evaluate_prepared_pose(input,output);
}

GripResults evaluate_prepared_pose(PoseInput const& input,SkeletonPose& output)
{
    GripResults results{};
    if(!input.grip)return results;

    math::float4x4 world_to_mesh{},skeleton_to_mesh{};
    if(!world_to_mesh.try_invert(input.mesh_world) ||
       !skeleton_to_mesh.try_invert(input.mesh_to_skeleton))
        throw std::runtime_error("Non-invertible equipped pose");
    auto const world_to_skeleton=world_to_mesh*input.mesh_to_skeleton;
    const float yaw=input.grip->yaw;
    // TPS shoulders aim in the character heading frame; the orbit camera
    // can pitch independently without bending the arms behind the back.
    const float pitch=input.grip->first_person?
        std::clamp(input.grip->pitch,-1.3f,1.3f):0.f;
    const Vector3 forward{
        std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    const Vector3 right{std::cos(yaw),0.f,-std::sin(yaw)};
    // Camera-space vertical is perpendicular to the actual aim direction.
    // World Y below caused wrist and elbow goals to diverge when looking
    // up or down, pulling the two arms apart in first-person mode.
    const Vector3 up{-std::sin(yaw)*std::sin(pitch),
        std::cos(pitch),-std::cos(yaw)*std::sin(pitch)};
    // WeaponLab pistol calibration: grip-hand axes in sight space are
    // {forward, -right, -up}. This is a proper rotation (determinant +1).
    // Both wrists receive this common weapon orientation, independently
    // of the body/eye anchor supplying their positions.
    math::float4x4 grip_basis;
    grip_basis.identity();
    const Vector3 grip_rows[3]{
        forward,{-right.x,-right.y,-right.z},{-up.x,-up.y,-up.z}
    };
    for(unsigned row=0;row<3;++row) {
        grip_basis.elements[row][0]=grip_rows[row].x;
        grip_basis.elements[row][1]=grip_rows[row].y;
        grip_basis.elements[row][2]=grip_rows[row].z;
    }
    // Wrist-local axes are not guaranteed to be XYZ on imported rigs.
    // The target matrices below are in a CANONICAL hand frame, not the
    // skeleton's raw joint frame. Convert using bind-space finger/thumb axes.
    // The fallback retains WeaponLab's historical calibration for rigs
    // without sufficient finger evidence.
    // The support palm faces the primary palm across the pistol's grip.
    // A 180-degree roll around the shared forearm axis supplies a valid
    // opposite-hand orientation without altering finger bind helpers.
    math::float4x4 support_basis=grip_basis;
    for(unsigned column=0;column<3;++column) {
        support_basis.elements[1][column]=-support_basis.elements[1][column];
        support_basis.elements[2][column]=-support_basis.elements[2][column];
    }
    auto const humanoid=input.skeleton.humanoid.valid?
        input.skeleton.humanoid:detect_canonical_humanoid_skeleton(input.skeleton);
    // Validate imported left/right against the current animated camera pose.
    // Bind-space may be reflected by the importer; never swap on names alone.
    bool switch_sides=false;
    if(input.grip->first_person && humanoid.valid) {
        auto const named_r=input.skeleton.joint_index("r_shoulder");
        auto const named_l=input.skeleton.joint_index("l_shoulder");
        if(named_r<input.skeleton.joints.size() &&
           named_l<input.skeleton.joints.size()) {
            const auto world_raw=[&](std::size_t i) {
                return (output.joints_world()[i]*skeleton_to_mesh*
                    input.mesh_world).transform_position({0.f,0.f,0.f});
            };
            auto const side=world_raw(named_r)-world_raw(named_l);
            float const lateral=side.x*right.x+side.y*right.y+
                                side.z*right.z;
            if(std::abs(lateral)>.14f)switch_sides=lateral<0.f;
        }
    }
    auto canonical_wrist_rotation=[&](unsigned hand,
                                     math::float4x4 const& desired) {
        if(!humanoid.valid || !humanoid.hand_basis_valid[hand])
            return math::quaternion(desired).normalize();
        const unsigned basis_hand=hand^
            (humanoid.side_repaired!=switch_sides?1u:0u);
        if(!humanoid.hand_basis_valid[basis_hand])
            return math::quaternion(desired).normalize();
        const auto finger=humanoid.finger_axis[basis_hand];
        const auto thumb=humanoid.thumb_axis[basis_hand];
        const auto normal=math::cross_product(finger,thumb);
        if(normal.magnitude()<.5f)
            return math::quaternion(desired).normalize();
        // Row-vector convention: local finger/thumb axes become the
        // canonical +X/+Y axes before applying the authored grip rotation.
        math::float4x4 local_basis;
        local_basis.identity();
        const math::float3 rows[3]{finger,thumb,normal};
        for(unsigned row=0;row<3;++row)
            for(unsigned col=0;col<3;++col)
                local_basis.elements[row][col]=rows[row][col];
        const auto rotation=math::transpose(local_basis)*desired;
        return math::quaternion(rotation).normalize();
    };
    // Legacy TPS attachments contain an authored rotation relative to
    // the original WeaponLab wrist basis. Do not double-retarget that
    // rotation until a TPS socket-orientation profile is available.
    const auto grip_rotation=input.grip->first_person?
        canonical_wrist_rotation(0,grip_basis):
        math::quaternion(grip_basis).normalize();
    const auto support_rotation=input.grip->first_person?
        canonical_wrist_rotation(1,support_basis):
        math::quaternion(support_basis).normalize();
    auto canonical_name=[&](char const* name)->char const* {
        if(!input.grip->first_person || !humanoid.valid)return name;
        std::size_t index=CanonicalHumanoidSkeleton::missing;
        std::string_view const requested{name};
        if(requested=="r_shoulder")index=humanoid.r_upperarm;
        else if(requested=="l_shoulder")index=humanoid.l_upperarm;
        else if(requested=="r_elbow")index=humanoid.r_forearm;
        else if(requested=="l_elbow")index=humanoid.l_forearm;
        else if(requested=="r_wrist")index=humanoid.r_hand;
        else if(requested=="l_wrist")index=humanoid.l_hand;
        else if(requested=="r_hand_prop_attachment")index=humanoid.r_hand_prop;
        else if(requested=="l_hand_prop_attachment")index=humanoid.l_hand_prop;
        if(humanoid.side_repaired!=switch_sides) {
            if(requested=="r_shoulder")index=humanoid.l_upperarm;
            else if(requested=="l_shoulder")index=humanoid.r_upperarm;
            else if(requested=="r_elbow")index=humanoid.l_forearm;
            else if(requested=="l_elbow")index=humanoid.r_forearm;
            else if(requested=="r_wrist")index=humanoid.l_hand;
            else if(requested=="l_wrist")index=humanoid.r_hand;
            else if(requested=="r_hand_prop_attachment")index=humanoid.l_hand_prop;
            else if(requested=="l_hand_prop_attachment")index=humanoid.r_hand_prop;
        }
        return index<input.skeleton.joints.size()?
            input.skeleton.joints[index].name.c_str():name;
    };
    auto joint_world=[&](char const* name) -> std::optional<Vector3> {
        auto const joint=find_joint(input.skeleton,canonical_name(name));
        if(joint==input.skeleton.joints.end())return std::nullopt;
        auto const index=static_cast<std::size_t>(joint-input.skeleton.joints.begin());
        return (output.joints_world().at(index)*skeleton_to_mesh*input.mesh_world)
            .transform_position({0.f,0.f,0.f});
    };
    // Determine the lateral sign from the real animated rig, not from
    // camera +X: imported YMT shoulder conventions need not match it.
    auto const shoulder_r=joint_world("r_shoulder");
    auto const shoulder_l=joint_world("l_shoulder");
    const float anatomical_sign=shoulder_r && shoulder_l &&
        ((*shoulder_r-*shoulder_l).x*right.x+
         (*shoulder_r-*shoulder_l).z*right.z)<0.f ? -1.f:1.f;
    // Use the actual mapped shoulder handedness for BOTH first-person and
    // third-person IK. Imported mirrored YMT rigs can place the canonical
    // right shoulder on camera-left; assuming +camera-right for FPS bent its
    // elbows inward and drove the supporting hand across the iron sights.
    // Keep the gun anchored to the same physical r_hand_prop_attachment.
    const Vector3 arm_right=right*anatomical_sign;
    auto const* authored=input.grip->finger_profile;
    auto const weapon_offset=[&](math::float3 local) {
        return arm_right*local.x+up*local.y+forward*local.z;
    };
    // Frame-local humanoid self-collision volume derived from animated joints.
    CharacterSelfCollision self_collision;
    const auto pelvis=joint_world("pelvis");
    const auto chest=joint_world("spineb");
    const auto neck=joint_world("spined");
    const auto head=joint_world("headb");
    if(pelvis && chest && neck && head && shoulder_r && shoulder_l)
        self_collision=make_character_self_collision(*pelvis,*chest,*neck,
            *head,*shoulder_r,*shoulder_l,{std::sin(yaw),0.f,std::cos(yaw)});
    std::array<bool,2> clavicle_adjusted{false,false};
    auto aim=[&](std::size_t hand,char const* shoulder,char const* elbow,
                 char const* wrist,GripSolveTarget const& grip) {
        const bool fps=input.grip->first_person;
        const Vector3 goal=grip.wrist_position;
        const auto target=world_to_skeleton.transform_position(goal);
        const auto safe_pole=self_collision.active?
            self_collision.outside(grip.elbow_pole,.045f):grip.elbow_pole;
        const auto elbow_pole=world_to_skeleton.transform_position(safe_pole);
        bool solved=false;
        // A single bounded two-bone pass can leave a residual on asymmetric
        // authored bind poses. Refine only while convergence improves, and
        // stop early to preserve the realtime animation budget.
        float error=std::numeric_limits<float>::infinity();
        for(unsigned iteration=0;iteration<(fps?
            (hand==1 && input.grip->aim_down_sights && authored &&
             authored->ads_support_hand_stiffness>1.1f?4u:3u):1u);
            ++iteration) {
            const bool moved=output.pose_two_bone(input.skeleton,
                canonical_name(shoulder),canonical_name(elbow),
                canonical_name(wrist),target,elbow_pole,
                fps?1.55f:1.30f,fps?1.80f:1.55f);
            if(!moved)break;
            solved=true;
            auto const actual=joint_world(wrist);
            if(!actual)break;
            const float next=(goal-*actual).magnitude();
            if(next<.006f || next>=error-.0002f)break;
            error=next;
        }
        // Targets can clear the torso while the solved elbow intersects it.
        // Only in that case refine the elbow pole without moving the wrist.
        if(self_collision.active) {
            for(unsigned pass=0;pass<3;++pass) {
                const auto e=joint_world(elbow);
                if(!e)break;
                const float intrusion=self_collision.correction(*e,.047f);
                if(intrusion<=.004f)break;
                const auto side=hand==0?arm_right:arm_right*(-1.f);
                const auto shoulder_pos=joint_world(shoulder);
                const auto wrist_pos=joint_world(wrist);
                Vector3 pole{};
                bool selected=false;
                if(fps && shoulder_pos && wrist_pos) {
                    const auto upper=(*e-*shoulder_pos).magnitude();
                    const auto lower=(*e-*wrist_pos).magnitude();
                    const auto candidate=choose_collision_aware_elbow_pole(
                        self_collision,*shoulder_pos,goal,*e,safe_pole,
                        side,upper,lower,.047f);
                    if(candidate.valid && candidate.predicted_intrusion+
                       .001f<intrusion) {
                        pole=candidate.position;
                        selected=true;
                    }
                }
                if(!selected) {
                    // Keep the former bounded correction for invalid or
                    // unreachable circle projections (no pose disruption).
                    const float extra=hand==1?.060f:.035f;
                    pole=*e+self_collision.forward*(intrusion+extra)+
                         side*(hand==1?.085f:.065f);
                }
                output.pose_two_bone(input.skeleton,canonical_name(shoulder),
                    canonical_name(elbow),canonical_name(wrist),
                    target,world_to_skeleton.transform_position(pole),
                    fps?1.55f:1.30f,fps?1.80f:1.55f);
            }
        }
        // A fixed shoulder makes some steep ADS poses geometrically
        // impossible without crossing the torso. Move the *actual ancestor
        // clavicle* by a bounded scapular glide, then re-solve both bones
        // against the original wrist/socket goal. This is only active on
        // colliding first-person poses, not during ordinary animation.
        if(fps && self_collision.active && !clavicle_adjusted[hand]) {
            const auto e=joint_world(elbow);
            const auto s=joint_world(shoulder);
            if(e && s && self_collision.correction(*e,.045f)>.030f) {
                const auto shoulder_idx=input.skeleton.joint_index(
                    canonical_name(shoulder));
                std::size_t clavicle_idx=missing;
                // Derive the clavicle from the actual hierarchy rather than
                // left/right names, which may be reflected in imported YMT.
                for(auto idx: {humanoid.r_clavicle,humanoid.l_clavicle}) {
                    if(idx>=input.skeleton.joints.size() ||
                       shoulder_idx>=input.skeleton.joints.size())continue;
                    auto child=shoulder_idx;
                    for(std::size_t n=0;n<input.skeleton.joints.size();++n) {
                        if(child==idx){clavicle_idx=idx;break;}
                        auto const parent=input.skeleton.joints[child].parent;
                        if(parent<0)break;
                        child=static_cast<std::size_t>(parent);
                    }
                    if(clavicle_idx!=missing)break;
                }
                if(clavicle_idx!=missing) {
                    const auto parent=input.skeleton.joints[clavicle_idx].parent;
                    math::float4x4 from_world;
                    const auto parent_world=parent<0?
                        skeleton_to_mesh*input.mesh_world:
                        output.joints_world()[static_cast<std::size_t>(parent)]*
                        skeleton_to_mesh*input.mesh_world;
                    if(from_world.try_invert(parent_world)) {
                        const auto side=hand==0?arm_right:arm_right*(-1.f);
                        // Shoulder-girdle excursion: <= 26 mm per posed
                        // frame; never move the hand or weapon directly.
                        const auto delta_world=side*.021f+
                            self_collision.forward*.012f;
                        const auto local_delta=from_world.transform_direction(
                            delta_world);
                        if(local_delta.valid() && local_delta.magnitude()<.08f &&
                           output.adjust_local_joint(input.skeleton,
                               input.skeleton.joints[clavicle_idx].name,
                               local_delta,{0.f,1.f,0.f},0.f)) {
                            clavicle_adjusted[hand]=true;
                            const auto moved_shoulder=joint_world(shoulder);
                            const auto moved_elbow=joint_world(elbow);
                            const auto moved_wrist=joint_world(wrist);
                            if(moved_shoulder && moved_elbow && moved_wrist) {
                                const auto pole=choose_collision_aware_elbow_pole(
                                    self_collision,*moved_shoulder,goal,
                                    *moved_elbow,safe_pole,side,
                                    (*moved_elbow-*moved_shoulder).magnitude(),
                                    (*moved_wrist-*moved_elbow).magnitude(),
                                    .047f);
                                const auto final_pole=pole.valid?
                                    pole.position:safe_pole;
                                for(unsigned refinement=0;refinement<3;
                                    ++refinement)
                                    output.pose_two_bone(input.skeleton,
                                        canonical_name(shoulder),
                                        canonical_name(elbow),canonical_name(wrist),
                                        target,world_to_skeleton.transform_position(
                                            final_pole),1.55f,1.80f);
                            }
                        }
                    }
                }
            }
        }
        // Final ADS silhouette pass. A torso-safe elbow can still put the
        // sleeve in front of the sights when the forearm is nearly sideways.
        // Search the reachable elbow circle; never move the weapon/wrist
        // target or allow the pose to penetrate the torso.
        if(fps && input.grip->aim_down_sights && self_collision.active &&
           input.grip->eye) {
            const auto e=joint_world(elbow);
            const auto s=joint_world(shoulder);
            const auto w=joint_world(wrist);
            const float required_depth=hand==0?.145f:.120f;
            if(e && s && w) {
                const float initial_depth=CharacterSelfCollision::dot(
                    goal-*e,self_collision.forward);
                const float initial_clearance=ads_forearm_sight_clearance(
                    *input.grip->eye,forward,*e,goal);
                // A kinematically valid elbow is not necessarily visible:
                // bulky sleeves can cover the camera ray even when the hand
                // and iron sights are perfectly aligned. Re-solve against
                // the optical corridor whenever the sleeve approaches it.
                if(initial_depth<required_depth-.008f ||
                   initial_clearance<(authored?
                       authored->ads_forearm_visibility_guard:.235f)) {
                    const auto side=hand==0?arm_right:arm_right*(-1.f);
                    const auto pole=choose_collision_aware_elbow_pole(
                        self_collision,*s,goal,*e,safe_pole,side,
                        (*e-*s).magnitude(),(*w-*e).magnitude(),
                        .045f,required_depth,*input.grip->eye,forward,
                        authored?authored->ads_forearm_visibility_guard:.235f);
                    if(pole.valid) {
                        auto const original_pose=output;
                        bool reached=false;
                        for(unsigned refinement=0;refinement<3;++refinement)
                            reached=output.pose_two_bone(
                                input.skeleton,canonical_name(shoulder),
                                canonical_name(elbow),canonical_name(wrist),
                                target,world_to_skeleton.transform_position(
                                    pole.position),1.90f,2.0f) || reached;
                        const auto revised_elbow=joint_world(elbow);
                        const auto revised_wrist=joint_world(wrist);
                        const float revised_clearance=revised_elbow?
                            ads_forearm_sight_clearance(*input.grip->eye,
                                forward,*revised_elbow,goal):0.f;
                        const float revised_depth=revised_elbow?
                            CharacterSelfCollision::dot(
                                goal-*revised_elbow,self_collision.forward):0.f;
                        const bool improved=reached && revised_elbow &&
                            revised_wrist &&
                            (revised_depth>initial_depth+.012f ||
                             revised_clearance>initial_clearance+.012f) &&
                            revised_depth>required_depth-.040f &&
                            self_collision.correction(*revised_elbow,.045f)<.035f &&
                            goal.y-revised_elbow->y>.055f &&
                            (*revised_wrist-goal).magnitude()<.012f;
                        if(!improved)output=original_pose;
                    }
                }
            }
        }
        // Position alone does not establish a weapon grip. After shoulder
        // and elbow IK, orient the actual wrist around its own joint pivot.
        // This preserves bone lengths and updates the correct rig helpers.
        auto const desired=math::create_rotation(grip.wrist_rotation)*
            world_to_skeleton;
        const bool oriented=output.orient_wrist(input.skeleton,
            canonical_name(wrist),desired,fps?2.05f:1.65f);
        if(auto const actual=joint_world(wrist))
            results[hand]=GripResult{goal,*actual,solved||oriented};
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
            const float gun_drop=input.grip->anchor_drop;
            const Vector3 right_offset=authored?
                weapon_offset(authored->wrist_offset[0]):
                arm_right*(-.032f)-up*.008f-forward*.068f;
            const Vector3 left_offset=authored?
                weapon_offset(authored->wrist_offset[1]+authored->support_offset):
                arm_right*(.028f+.030f)-
                    up*(.018f+.020f)+forward*(-.054f+.020f);
            // Raise the shared gun-and-hands anchor into the eye's lower
            // field of view and move it to the dominant (right) side.
            auto const base=eye-up*gun_drop+arm_right*input.grip->anchor_right;
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
                const float depth=std::clamp(input.grip->anchor_forward,min_depth,max_depth);
                const Vector3 anchor=base+forward*(depth-input.grip->recoil_back);
                // Elbows bend forward and outside the torso instead of
                // crossing the chest or escaping behind the shoulders.
                // Elbow poles must stay in the BODY forward/up frame:
                // camera pitch may point into the torso when aiming down.
                // Pitching the poles with the camera produced crossed elbows
                // and penetration at -1.1 rad, even with safe hand targets.
                const Vector3 body_forward{std::sin(yaw),0.f,std::cos(yaw)};
                const Vector3 body_up{0.f,1.f,0.f};
                // ADS silhouette: bend the elbows below the forearms,
                // out of the eye-to-rear-sight occlusion cone. The old pole
                // was too far forward/high and brought Abby's right sleeve
                // across the rear notch even when wrists met both sockets.
                // ADS must keep the elbows below and OUTSIDE the sight
                // corridor. A camera-centred hand is expected, but the
                // forearm sleeve must rise from the lower corners, never
                // pass in front of the rear notch. Use world-up poles so
                // looking down cannot lift the elbows into the camera.
                const bool aiming=input.grip->aim_down_sights;
                const Vector3 pole_right=*shoulder_right+
                    arm_right*(aiming?(authored?
                        authored->ads_elbow_outward_bias:.39f):.24f)+
                    body_forward*.15f-
                    body_up*(aiming?(authored?
                        authored->ads_elbow_drop_bias:.40f):.33f)-
                    body_up*(input.grip->recoil_arm*.020f);
                const Vector3 pole_left=*shoulder_left-
                    arm_right*(aiming?(authored?
                        authored->ads_elbow_outward_bias*.95f:.37f):
                        (authored?.30f:.24f))+
                    body_forward*.16f-
                    body_up*(aiming?(authored?
                        authored->ads_elbow_drop_bias*.975f:.39f):.31f)-
                    body_up*(input.grip->recoil_arm*.018f);
                const auto primary=anchor+right_offset;
                // Check the grip, midpoint and muzzle against torso/head.
                const float clearance=self_collision.weapon_correction(
                    primary,forward,.24f,.048f);
                auto safe_primary=primary+self_collision.forward*clearance;
                aim(0,"r_shoulder","r_elbow","r_wrist",
                    GripSolveTarget{safe_primary,grip_rotation,pole_right});
                if(input.grip->aim_down_sights && authored) {
                    // The physical rear notch (not an arbitrary wrist pivot)
                    // must lie on the camera sight ray. Solve the primary hand
                    // towards the point where its real socket places the
                    // weapon's rear sight in front of the viewer. Iterate only
                    // twice to keep this pose bounded and deterministic.
                    auto const rear=authored->rear_sight;
                    const auto rear_offset=right*rear.x+up*rear.y+forward*rear.z;
                    const auto desired_socket=eye+
                        forward*(authored->sight_eye_distance-input.grip->recoil_back)-rear_offset;
                    for(unsigned iteration=0;iteration<2;++iteration) {
                        auto current_socket=joint_world("r_hand_prop_attachment");
                        if(!current_socket)break;
                        auto correction=desired_socket-*current_socket;
                        float const residual=correction.magnitude();
                        if(residual<.004f)break;
                        if(!std::isfinite(residual) || residual>.35f)break;
                        safe_primary=safe_primary+correction;
                        if(self_collision.active)
                            safe_primary=self_collision.outside(safe_primary,.035f);
                        aim(0,"r_shoulder","r_elbow","r_wrist",
                            GripSolveTarget{safe_primary,grip_rotation,pole_right});
                    }
                }
                // Unlike the old right-wrist approximation, the handgun
                // anchor is the REAL animated r_hand_prop_attachment
                // socket. The NewViso WeaponLab pistol's support/grip
                // offsets are expressed relative to that same firearm.
                auto const socket=joint_world("r_hand_prop_attachment");
                // WeaponLab source: left hand grasps the opposite side of
                // the SAME firearm grip, not an unrelated camera-space point.
                // The child bone socket and its exact imported basis are
                // already resolved by the right-hand IK stage above.
                const auto support_delta=authored && authored->has_support_grip?
                    weapon_offset(authored->support_grip-authored->primary_grip):
                    authored?weapon_offset(authored->wrist_offset[1]+
                        authored->support_offset-authored->socket_offset[0]):
                        -arm_right*.055f-up*.025f+forward*.02f;
                Vector3 support_goal=socket?
                    *socket+support_delta:
                    safe_primary+support_delta;
                // Opposite wrist sides are invariant in the shoulder's real
                // lateral basis; never compare world X on mirrored imports.
                const auto lateral=support_goal-safe_primary;
                float const lateral_gap=lateral.x*arm_right.x+
                    lateral.y*arm_right.y+lateral.z*arm_right.z;
                if(lateral_gap>-.052f)
                    support_goal=support_goal-arm_right*(lateral_gap+.052f);
                const auto safe_support=self_collision.active?
                    self_collision.outside(support_goal,.040f):support_goal;
                aim(1,"l_shoulder","l_elbow","l_wrist",
                    GripSolveTarget{safe_support,support_rotation,pole_left});
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
            arm_right*(fps?.075f:.065f)-up*(fps?.16f:.15f);
        // The elbow pole is below and slightly behind the aiming hands,
        // keeping the forearms coming up from the lower corners of the view.
        const Vector3 elbow_pole=*shoulder+arm_right*.18f+
            forward*(fps?.25f:.13f)-up*(fps?.18f:.24f);
        const float clearance=self_collision.weapon_correction(
            target,forward,.24f,.048f);
        const auto safe_target=target+self_collision.forward*clearance;
        aim(0,"r_shoulder","r_elbow","r_wrist",
            GripSolveTarget{safe_target,grip_rotation,elbow_pole});
        if(auto const wrist=joint_world("r_wrist"))
            if(auto const left=joint_world("l_shoulder")) {
                // Both TPS and FPS resolve the support wrist against the
                // firearm's authored support socket, when one exists.
                // Keep the legacy stance only for assets without sockets.
                const auto socket=joint_world("r_hand_prop_attachment");
                Vector3 support=authored && authored->has_support_grip && socket?
                    *socket+weapon_offset(authored->support_grip-authored->primary_grip):
                    *wrist-arm_right*.120f+forward*.075f-up*.025f;
                const auto lateral=support-*wrist;
                const float separation=lateral.x*arm_right.x+
                    lateral.y*arm_right.y+lateral.z*arm_right.z;
                if(separation>-.052f)
                    support=support-arm_right*(separation+.052f);
                const Vector3 support_pole=*left-arm_right*.16f+
                    forward*(fps?.25f:.13f)-up*(fps?.18f:.24f);
                const auto safe_support=self_collision.active?
                    self_collision.outside(support,.040f):support;
                aim(1,"l_shoulder","l_elbow","l_wrist",
                    GripSolveTarget{safe_support,support_rotation,support_pole});
            }
    }
    // Imported WeaponLab pose is a LOCAL finger rotation layer on the
    // already IK-positioned wrists. SkeletonPose updates mirrored helpers
    // and the cached secondary palette constraints in the same pass.
    // All thirty retargeted joints must exist; otherwise no partial curl.
    if(authored)output.apply_finger_bends(input.skeleton,authored->bends,1.f);
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
        // Prepublish the opposite physical socket only for opt-in FPS
        // attachment resolution. Authored third-person sockets remain intact.
        auto const mirror=bone=="r_hand_prop_attachment"?
            skeleton.joint_index("l_hand_prop_attachment"):
            bone=="l_hand_prop_attachment"?
            skeleton.joint_index("r_hand_prop_attachment"):skeleton.joints.size();
        if(mirror<skeleton.joints.size())
            joints.emplace("__mirrored_"+bone,
                (evaluated.joints_world().at(mirror)*skeleton_to_mesh)*mesh_world);
    }
    // Publish both shoulder positions, allowing camera-aligned attachments
    // to validate handedness in the same space as the actual first-person
    // scene, without trusting a possibly reflected bind-pose basis.
    for(auto const* name:{"r_shoulder","l_shoulder"}) {
        auto const index=skeleton.joint_index(name);
        if(index<skeleton.joints.size())
            joints.emplace(std::string("__named_")+name,
                (evaluated.joints_world()[index]*skeleton_to_mesh)*mesh_world);
    }
    // Preserve a body/head proximity proxy for socket-driven attachments.
    // Evaluate it from the identical bone matrices used for the skinned mesh.
    auto world_joint=[&](char const* name)->std::optional<Vector3> {
        const auto index=skeleton.joint_index(name);
        if(index>=skeleton.joints.size())return {};
        return (evaluated.joints_world()[index]*skeleton_to_mesh*mesh_world)
            .transform_position({0.f,0.f,0.f});
    };
    const auto pelvis=world_joint("pelvis");
    const auto chest=world_joint("spineb");
    const auto neck=world_joint("spined");
    const auto head=world_joint("headb");
    const auto eye=world_joint("r_eyeball");
    const auto right_shoulder=world_joint("r_shoulder");
    const auto left_shoulder=world_joint("l_shoulder");
    if(pelvis && chest && neck && head && eye && right_shoulder && left_shoulder)
        self_collisions_[id]=make_character_self_collision(
            *pelvis,*chest,*neck,*head,*right_shoulder,*left_shoulder,
            *eye-*head);
}

std::optional<math::float4x4> SkeletalSceneFrame::attachment_transform(
    BoneAttachment const& attachment, CameraPose const* camera) const
{
    auto const origin=sockets_.find(attachment.parent);
    if(origin==sockets_.end())return {};
    auto joint=origin->second.find(attachment.bone);
    if(attachment.camera_forward && camera) {
        auto const shoulder_r=origin->second.find("__named_r_shoulder");
        auto const shoulder_l=origin->second.find("__named_l_shoulder");
        if(shoulder_r!=origin->second.end() &&
           shoulder_l!=origin->second.end()) {
            auto const r=shoulder_r->second.transform_position({0,0,0});
            auto const l=shoulder_l->second.transform_position({0,0,0});
            const Vector3 camera_right{std::cos(camera->yaw),0.f,
                -std::sin(camera->yaw)};
            auto const delta=r-l;
            float const lateral=delta.x*camera_right.x+
                delta.y*camera_right.y+delta.z*camera_right.z;
            if(lateral<-.14f) {
                auto const mirrored=origin->second.find(
                    "__mirrored_"+attachment.bone);
                if(mirrored!=origin->second.end())joint=mirrored;
            }
        }
    }
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
        // Last-resort guard for camera-aligned attachments (e.g. equipped
        // firearms). Prevent the *actual* published socket transform from
        // placing its proximal mesh or barrel inside the owner body.
        // Ordinary 'socket' attachments are not automatically moved: holsters
        // and clothing can legitimately overlap the torso.
        if(const auto entry=self_collisions_.find(attachment.parent);
           entry!=self_collisions_.end() && entry->second.active) {
            const Vector3 origin{
                transform.elements[3][0],transform.elements[3][1],
                transform.elements[3][2]};
            auto direction=transform.transform_direction({0.f,0.f,1.f});
            const float length=direction.magnitude();
            if(std::isfinite(length) && length>.01f) {
                direction=direction/length;
                // The imported eye/head bone offset is not a reliable body
                // facing axis: some YMT rigs report a backwards diagonal.
                // For camera-aligned weapons use the real sight direction
                // to resolve body clearance. Otherwise an already aligned
                // rear sight is pushed sideways and *behind* the camera.
                auto clearance=entry->second;
                Vector3 planar{direction.x,0.f,direction.z};
                const float planar_length=planar.magnitude();
                if(planar_length>.02f)
                    clearance.forward=planar/planar_length;
                const float shift=clearance.weapon_correction(
                    origin,direction,.24f,.048f);
                transform.elements[3][0]+=clearance.forward.x*shift;
                transform.elements[3][1]+=clearance.forward.y*shift;
                transform.elements[3][2]+=clearance.forward.z*shift;
            }
        }
    }
    return transform;
}
} // namespace northstarcpp::viewport_detail
