#include <northstarcpp/grounded_locomotion.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace northstarcpp {
namespace {
using math::float3;
float3 translation(math::float4x4 const& m) {
    return {m.elements[3][0],m.elements[3][1],m.elements[3][2]};
}
bool valid(float x) {return std::isfinite(x);}
bool sane(float3 v) {
    return valid(v.x)&&valid(v.y)&&valid(v.z);
}
float3 unit(float3 v,float3 fallback={0,1,0}) {
    auto const magnitude=v.magnitude();
    return magnitude>.0001f?v/magnitude:fallback;
}
float alpha(float dt,float period) {
    return period<=0.f?1.f:1.f-std::exp(-dt/period);
}
}
GroundedLocomotionController::GroundedLocomotionController(Skeleton const& rig,
    std::vector<GroundedFootDefinition> feet,GroundedBodyDefinition body)
    : rig_(&rig),body_(std::move(body)) {
    if(feet.empty()||feet.size()>4 || !valid(body_.max_pelvis_drop)||
       !valid(body_.max_torso_tilt) ||body_.max_pelvis_drop<0.f||
       body_.max_pelvis_drop>.25f||body_.max_torso_tilt<0.f||
       body_.max_torso_tilt>.2f)
        throw std::invalid_argument("Invalid Grounded Locomotion body or foot count");
    pelvis_=body_.pelvis.empty()?rig.joints.size():
        rig.joint_index(body_.pelvis);
    torso_=body_.torso.empty()?rig.joints.size():
        rig.joint_index(body_.torso);
    if((!body_.pelvis.empty()&&pelvis_>=rig.joints.size())||
       (!body_.torso.empty()&&torso_>=rig.joints.size()))
        throw std::invalid_argument("Unknown grounded pelvis or torso bone");
    for(auto& definition:feet) {
        auto const ankle=rig.joint_index(definition.ankle);
        if(definition.chain.empty() ||ankle>=rig.joints.size() ||
           !sane(definition.heel_offset)||!sane(definition.toe_offset)||
           !valid(definition.clearance)||!valid(definition.max_raise)||
           !valid(definition.max_drop) ||definition.clearance<0.f||
           definition.clearance>.10f||definition.max_raise<0.f||
           definition.max_raise>.5f||definition.max_drop<0.f||
           definition.max_drop>.5f)
            throw std::invalid_argument("Invalid Grounded Locomotion foot definition");
        for(auto const& previous:feet_)
            if(previous.def.chain==definition.chain)
                throw std::invalid_argument("Duplicate Grounded Locomotion chain");
        Foot foot;
        foot.toe=rig.joints.size();
        if(!definition.toe_bone.empty()) {
            auto const toe=rig.joint_index(definition.toe_bone);
            bool descendant=false;
            auto node=toe;
            for(int depth=0;depth<6 &&node<rig.joints.size();++depth) {
                if(node==ankle){descendant=true;break;}
                auto const parent=rig.joints[node].parent;
                if(parent<0)break;
                node=static_cast<std::size_t>(parent);
            }
            if(!descendant ||toe==ankle)
                throw std::invalid_argument("Toe bone must descend from ankle");
            foot.toe=toe;
        }
        foot.def=std::move(definition);
        foot.ankle=ankle;
        feet_.push_back(std::move(foot));
    }
    reports_.reserve(feet_.size());
}
void GroundedLocomotionController::reset() noexcept {
    pelvis_offset_=torso_tilt_=0.f;
    for(auto& foot:feet_) {
        foot.heel_ticket=foot.toe_ticket=0;
        foot.contacts={};
        foot.has_last=foot.planted=foot.has_contact=false;
        foot.locked_platform=UINT32_MAX;
        foot.toe_pitch=0.f;
        foot.phase_explicit=foot.phase_planted=false;
        foot.time_without_contact=foot.plant_age=foot.contact_age=0.f;
    }
    reports_.clear();
}
bool GroundedLocomotionController::provide_contact(
    std::string_view chain,GroundedFootContact contact) {
    for(auto& foot:feet_) {
        if(foot.def.chain!=chain)continue;
        foot.contacts=std::move(contact);
        foot.has_contact=true;
        foot.time_without_contact=0.f;
        return true;
    }
    return false;
}
bool GroundedLocomotionController::set_foot_phase(
    std::string_view chain,bool planting) noexcept {
    for(auto& foot:feet_) {
        if(foot.def.chain!=chain)continue;
        foot.phase_explicit=true;
        foot.phase_planted=planting;
        if(!planting) {
            foot.planted=false;
            foot.locked_platform=UINT32_MAX;
        }
        return true;
    }
    return false;
}
void GroundedLocomotionController::finish_toes(SkeletonPose& pose) {
    if(pose.joints_world().size()!=rig_->joints.size())return;
    for(auto const& foot:feet_)
        if(foot.toe<rig_->joints.size() &&
           std::abs(foot.toe_pitch)>.0001f)
            pose.adjust_local_joint(*rig_,foot.def.toe_bone,
                {0,0,0},{1,0,0},foot.toe_pitch);
}

void GroundedLocomotionController::update(PhysicsScene const* physics,
    std::size_t ignored_body,SkeletonPose& pose,ProceduralIkController& ik,
    math::float4x4 const& rig_to_world,float dt,std::uint32_t channel) {
    reports_.clear();
    if(!valid(dt)||dt<0.f||dt>.25f ||channel>=16 ||
       pose.joints_world().size()!=rig_->joints.size())return;
    math::float4x4 world_to_rig;
    if(!world_to_rig.try_invert(rig_to_world))return;
    float wanted_pelvis=0.f;
    float3 aggregate_normal{0,0,0};
    unsigned normals=0;
    struct Desired {
        bool supported=false,planted=false;
        float3 target_world{},normal_world{0,1,0};
        float correction=0.f;
    };
    std::array<Desired,4> desired{};
    // Consume results submitted in an earlier animation frame; no physics
    // provider query is executed on this render/evaluation thread.
    for(std::size_t i=0;i<feet_.size();++i) {
        auto& foot=feet_[i];
        auto const& joint=pose.joints_world()[foot.ankle];
        auto const ankle=rig_to_world.transform_position(translation(joint));
        auto const heel=rig_to_world.transform_position(
            joint.transform_position(foot.def.heel_offset));
        auto const toe=rig_to_world.transform_position(
            joint.transform_position(foot.def.toe_offset));
        if(physics) {
            if(foot.heel_ticket) {
                auto result=physics->consume_ground_probe(foot.heel_ticket);
                if(result) {
                    foot.contacts.heel=*result;
                    foot.heel_ticket=0;
                    foot.has_contact=true;
                }
            }
            if(foot.toe_ticket) {
                auto result=physics->consume_ground_probe(foot.toe_ticket);
                if(result) {
                    foot.contacts.toe=*result;
                    foot.toe_ticket=0;
                    foot.has_contact=true;
                }
            }
            if(!foot.heel_ticket)
                foot.heel_ticket=physics->enqueue_ground_probe(
                    heel+float3{0,.22f,0},heel-float3{0,.48f,0},
                    channel,ignored_body);
            if(!foot.toe_ticket)
                foot.toe_ticket=physics->enqueue_ground_probe(
                    toe+float3{0,.22f,0},toe-float3{0,.48f,0},
                    channel,ignored_body);
        }
        bool invalidated=foot.has_last &&
            (ankle-foot.last_world).magnitude()>2.f;
        if(foot.planted &&foot.locked_platform!=UINT32_MAX) {
            auto const body=physics?
                physics->body_transform(foot.locked_platform):std::nullopt;
            if(!body)invalidated=true;
            else {
                auto const relocated=body->transform_position(
                    foot.locked_platform_local);
                if((relocated-foot.locked_world).magnitude()>1.f)
                    invalidated=true;
                else foot.locked_world=relocated;
            }
        }
        if(invalidated) {
            foot.planted=false;
            foot.locked_platform=UINT32_MAX;
            foot.contacts={};
            foot.phase_explicit=false;
            foot.toe_pitch=0.f;
        }
        foot.contact_age=foot.has_contact?0.f:foot.contact_age+dt;
        foot.has_contact=false;
        if(foot.contact_age>.35f) {
            foot.contacts={};
            foot.planted=false;
            foot.locked_platform=UINT32_MAX;
        }
        auto const h=foot.contacts.heel &&foot.contacts.heel->hit;
        auto const t=foot.contacts.toe &&foot.contacts.toe->hit;
        auto const supported=h||t;
        if(!supported)foot.toe_pitch=0.f;
        foot.time_without_contact=supported?0.f:
            foot.time_without_contact+dt;
        if(!supported &&foot.time_without_contact>.15f)
            foot.planted=false;
        auto& output=desired[i];
        output.supported=supported;
        auto& report=reports_.emplace_back();
        report.chain=foot.def.chain;
        report.heel_hit=h;
        report.toe_hit=t;
        if(supported) {
            auto const heel_delta=h?
                foot.contacts.heel->point.y-heel.y+
                    foot.def.clearance:-100.f;
            auto const toe_delta=t?
                foot.contacts.toe->point.y-toe.y+
                    foot.def.clearance:-100.f;
            output.correction=std::clamp(std::max(heel_delta,toe_delta),
                -foot.def.max_drop,foot.def.max_raise);
            foot.toe_pitch=(h&&t&&foot.toe<rig_->joints.size())?
                std::clamp((toe_delta-heel_delta)*1.2f,-.16f,.16f):0.f;
            output.target_world=ankle+float3{0,output.correction,0};
            output.normal_world=unit(
                (h?float3(foot.contacts.heel->normal):float3{0,0,0})+
                (t?float3(foot.contacts.toe->normal):float3{0,0,0}));
            aggregate_normal+=output.normal_world;
            ++normals;
            auto const speed=foot.has_last&&dt>.0001f?
                (ankle-foot.last_world).magnitude()/dt:0.f;
            if(foot.planted) {
                foot.plant_age+=dt;
                auto const separation=(ankle-foot.locked_world).magnitude();
                if((foot.phase_explicit&&!foot.phase_planted)||
                   (foot.locked_platform==UINT32_MAX&&
                    (speed>1.1f||separation>.42f))||
                   std::abs(output.correction)>.28f)
                    foot.planted=false;
            } else if(foot.has_last &&
                (foot.phase_explicit?foot.phase_planted:
                 (speed<.32f&&std::abs(output.correction)<.14f))) {
                foot.planted=true;
                foot.plant_age=0.f;
                foot.locked_world=output.target_world;
                foot.locked_platform=UINT32_MAX;
                auto const platform=h?foot.contacts.heel->object_index:
                    foot.contacts.toe->object_index;
                if(physics&&platform!=UINT32_MAX) {
                    auto const body=physics->body_transform(platform);
                    math::float4x4 inverse;
                    if(body && inverse.try_invert(*body)) {
                        foot.locked_platform_local=inverse.transform_position(
                            foot.locked_world);
                        foot.locked_platform=platform;
                    }
                }
            }
            if(foot.planted) {
                output.target_world=foot.locked_world;
                output.planted=true;
            }
            wanted_pelvis=std::min(wanted_pelvis,
                std::min(output.correction,0.f));
            report.target_adjustment=output.correction;
            report.planted=foot.planted;
            report.anchor_world=foot.planted?foot.locked_world:output.target_world;
            report.support_body=foot.planted?foot.locked_platform:UINT32_MAX;
            report.anchor_slip_meters=foot.planted?
                (ankle-foot.locked_world).magnitude():0.f;
        }
        foot.last_world=ankle;
        foot.has_last=true;
    }
    wanted_pelvis=std::clamp(wanted_pelvis,
        -body_.max_pelvis_drop,0.f);
    pelvis_offset_+=(wanted_pelvis-pelvis_offset_)*alpha(dt,.09f);
    // Move the pelvis just once, ahead of each leg solver.
    if(pelvis_<rig_->joints.size() &&std::abs(pelvis_offset_)>.0001f) {
        auto offset=world_to_rig.transform_direction(
            float3{0,pelvis_offset_,0});
        auto const parent=rig_->joints[pelvis_].parent;
        if(parent>=0) {
            math::float4x4 inv;
            if(inv.try_invert(pose.joints_world()[std::size_t(parent)]))
                offset=inv.transform_direction(offset);
        }
        pose.adjust_local_joint(*rig_,rig_->joints[pelvis_].name,
            offset,{0,1,0},0.f);
    }
    // A small torso slope lean may be authored separately from head aim.
    if(torso_<rig_->joints.size()) {
        auto normal=normals?unit(aggregate_normal):float3{0,1,0};
        normal=unit(world_to_rig.transform_direction(normal));
        auto const axis=math::cross_product(float3{0,1,0},normal);
        auto const slope=std::atan2(axis.magnitude(),
            std::clamp(normal.y,-1.f,1.f));
        auto const target=std::min(slope*.25f,body_.max_torso_tilt);
        torso_tilt_+=(target-torso_tilt_)*alpha(dt,.12f);
        if(axis.magnitude()>.0001f&&torso_tilt_>.0001f)
            pose.adjust_local_joint(*rig_,rig_->joints[torso_].name,
                {0,0,0},unit(axis),torso_tilt_);
    }
    for(std::size_t i=0;i<feet_.size();++i) {
        auto const& foot=feet_[i];
        auto const& goal=desired[i];
        if(!goal.supported)continue;
        auto const ankle_world=rig_to_world.transform_position(
            translation(pose.joints_world()[foot.ankle]));
        IkTargetRequest request;
        request.position=world_to_rig.transform_position(goal.target_world);
        // Preserve the animated bend direction, not a hardcoded knee pole.
        auto const knee=rig_->joints[foot.ankle].parent;
        if(knee<0)continue;
        auto const pole=translation(
            pose.joints_world()[static_cast<std::size_t>(knee)]);
        request.pole=pole;
        request.weight=goal.planted?1.f:.75f;
        request.priority=goal.planted?35:20;
        request.align_effector=true;
        request.max_tilt_radians=.35f;
        request.up_normal=unit(world_to_rig.transform_direction(
            goal.normal_world));
        (void)ankle_world;
        ik.submit(foot.def.chain,request);
    }
}
}
