#include <northstarcpp/procedural_ik.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace northstarcpp {
namespace {
using math::float3;
bool finite(float x) noexcept {return std::isfinite(x);}
bool finite_vec(float3 const& p) noexcept {
    return finite(p.x)&&finite(p.y)&&finite(p.z);
}
float3 origin(math::float4x4 const& transform) {
    return {transform.elements[3][0],transform.elements[3][1],
            transform.elements[3][2]};
}
float3 mix(float3 const& a,float3 const& b,float t) {
    return a+(b-a)*t;
}
float3 safe_unit(float3 const& v,float3 const& fallback) {
    auto const length=v.magnitude();
    return length>1.e-5f?v/length:fallback;
}
bool descendant(Skeleton const& rig,std::size_t child,std::size_t parent) {
    for(std::size_t i=0;i<rig.joints.size();++i) {
        if(child==parent)return true;
        auto const prev=rig.joints[child].parent;
        if(prev<0||static_cast<std::size_t>(prev)>=rig.joints.size())return false;
        child=static_cast<std::size_t>(prev);
    }
    return false; // malformed cycle
}
bool target_valid(IkTargetRequest const& r) {
    return finite_vec(r.position)&&finite_vec(r.pole)&&finite_vec(r.up_normal)&&
        finite(r.weight)&&r.weight>=0.f&&r.weight<=1.f&&
        finite(r.max_tilt_radians)&&r.max_tilt_radians>=0.f&&
        r.max_tilt_radians<=1.4f&&
        (!r.align_effector||r.up_normal.magnitude()>.5f);
}
void align_to_ground(SkeletonPose& pose,Skeleton const& rig,
    std::string const& effector,float3 const& normal,float max_tilt) {
    if(max_tilt<1.e-4f)return;
    auto const index=rig.joint_index(effector);
    if(index>=rig.joints.size())return;
    auto const current=pose.joints_world()[index];
    auto up=safe_unit(normal,{0,1,0});
    float3 forward{current.elements[2][0],current.elements[2][1],
                   current.elements[2][2]};
    forward=forward-up*math::dot_product(forward,up);
    if(forward.magnitude()<.0001f) {
        float3 side{current.elements[0][0],current.elements[0][1],
                    current.elements[0][2]};
        forward=math::cross_product(side,up);
    }
    forward=safe_unit(forward,{0,0,1});
    auto right=safe_unit(math::cross_product(up,forward),{1,0,0});
    forward=safe_unit(math::cross_product(right,up),{0,0,1});
    auto desired=current;
    for(unsigned col=0;col<3;++col) {
        desired.elements[0][col]=right[int(col)];
        desired.elements[1][col]=up[int(col)];
        desired.elements[2][col]=forward[int(col)];
    }
    pose.orient_wrist(rig,effector.c_str(),desired,max_tilt);
}
}
ProceduralIkController::ProceduralIkController(Skeleton const& rig)
    : rig_(&rig) {
    chains_.reserve(16);
    order_.reserve(16);
    reports_.reserve(16);
}
bool ProceduralIkController::register_chain(IkChainDefinition def) {
    if(def.name.empty()||def.name.size()>128||
       def.root.empty()||def.hinge.empty()||def.effector.empty()||
       chains_.size()>=16 || !finite(def.root_angle_limit) ||
       !finite(def.hinge_angle_limit) || def.root_angle_limit<=0.f||
       def.hinge_angle_limit<=0.f || def.root_angle_limit>3.14f||
       def.hinge_angle_limit>3.14f||!finite(def.blend_in_seconds)||
       !finite(def.blend_out_seconds)||def.blend_in_seconds<0.f||
       def.blend_out_seconds<0.f||def.blend_in_seconds>2.f||
       def.blend_out_seconds>2.f)return false;
    for(auto const& c:chains_)if(c.definition.name==def.name)return false;
    auto const r=rig_->joint_index(def.root);
    auto const h=rig_->joint_index(def.hinge);
    auto const t=rig_->joint_index(def.effector);
    if(r>=rig_->joints.size()||h>=rig_->joints.size()||
       t>=rig_->joints.size() ||r==h||h==t||
       !descendant(*rig_,h,r)||!descendant(*rig_,t,h))return false;
    Chain chain;
    chain.definition=std::move(def);
    chain.root=r;chain.hinge=h;chain.effector=t;
    chains_.push_back(std::move(chain));
    order_.push_back(chains_.size()-1);
    return true;
}
bool ProceduralIkController::submit(std::string_view id,IkTargetRequest goal) {
    if(!target_valid(goal))return false;
    for(auto& chain:chains_) {
        if(chain.definition.name!=id)continue;
        if(!chain.submitted||goal.priority>chain.priority) {
            chain.pending=std::move(goal);
            chain.priority=chain.pending.priority;
            chain.submitted=true;
        }
        return true;
    }
    return false;
}
bool ProceduralIkController::submit_ground(std::string_view id,
    IkGroundContact const& contact,IkTargetRequest request,
    SkeletonPose const& pose) {
    if(!contact.hit||!finite_vec(contact.point)||!finite_vec(contact.normal)||
       !finite(contact.foot_clearance)||!finite(contact.max_raise)||
       !finite(contact.max_drop)||contact.max_raise<0.f||
       contact.max_drop<0.f||contact.max_raise>2.f||contact.max_drop>2.f||
       contact.normal.magnitude()<.5f||contact.normal.y<.45f)return false;
    for(auto const& chain:chains_) {
        if(chain.definition.name!=id)continue;
        auto const& joints=pose.joints_world();
        if(joints.size()!=rig_->joints.size())return false;
        auto const ankle=origin(joints[chain.effector]);
        request.position=ankle;
        request.position.y+=std::clamp(
            contact.point.y+contact.foot_clearance-ankle.y,
            -contact.max_drop,contact.max_raise);
        request.pole=origin(joints[chain.hinge]);
        request.up_normal=contact.normal;
        request.align_effector=true;
        return submit(id,std::move(request));
    }
    return false;
}
void ProceduralIkController::evaluate(SkeletonPose& pose,float dt,
    std::size_t max_solvers) {
    reports_.clear();
    if(!finite(dt)||dt<0.f||dt>.25f ||
       pose.joints_world().size()!=rig_->joints.size()) {
        // Drop unusable frame requests instead of poisoning IK state.
        for(auto& chain:chains_)chain.submitted=false;
        return;
    }
    std::sort(order_.begin(),order_.end(),[this](auto a,auto b) {
        auto const& left=chains_[a];
        auto const& right=chains_[b];
        if(left.submitted!=right.submitted)return left.submitted;
        if(left.priority!=right.priority)return left.priority>right.priority;
        return a<b;
    });
    std::size_t processed=0;
    for(auto const id:order_) {
        auto& chain=chains_[id];
        auto const requested=chain.submitted;
        if(requested) {
            if(!chain.has_target) {
                chain.current=chain.pending;
                chain.has_target=true;
            } else {
                auto const seconds=chain.definition.blend_in_seconds;
                auto const alpha=seconds<=0.f?1.f:
                    std::clamp(1.f-std::exp(-dt/seconds),0.f,1.f);
                chain.current.position=mix(chain.current.position,
                    chain.pending.position,alpha);
                chain.current.pole=mix(chain.current.pole,chain.pending.pole,alpha);
                chain.current.up_normal=safe_unit(
                    mix(chain.current.up_normal,chain.pending.up_normal,alpha),
                    {0,1,0});
                chain.current.align_effector=chain.pending.align_effector;
                chain.current.max_tilt_radians=chain.pending.max_tilt_radians;
            }
        }
        auto const target=requested?chain.pending.weight:0.f;
        auto const seconds=requested?chain.definition.blend_in_seconds:
            chain.definition.blend_out_seconds;
        auto const gain=seconds<=0.f?1.f:
            std::clamp(1.f-std::exp(-dt/seconds),0.f,1.f);
        chain.current_weight+=
            (target-chain.current_weight)*gain;
        chain.current_weight=std::clamp(chain.current_weight,0.f,1.f);
        chain.submitted=false;
        IkSolveReport report{chain.definition.name};
        report.effective_weight=chain.current_weight;
        if(chain.current_weight>.001f &&processed<max_solvers) {
            ++processed;
            report.attempted=true;
            auto const& joint=pose.joints_world()[chain.effector];
            auto const now=origin(joint);
            auto const destination=mix(now,chain.current.position,
                chain.current_weight);
            report.solved=pose.pose_two_bone(*rig_,
                chain.definition.root.c_str(),chain.definition.hinge.c_str(),
                chain.definition.effector.c_str(),
                destination,chain.current.pole,
                chain.definition.root_angle_limit,
                chain.definition.hinge_angle_limit);
            if(chain.current.align_effector)
                align_to_ground(pose,*rig_,chain.definition.effector,
                    chain.current.up_normal,
                    chain.current.max_tilt_radians*chain.current_weight);
            report.residual_meters=(
                destination-origin(pose.joints_world()[chain.effector])).magnitude();
        }
        if(!requested&&chain.current_weight<=.001f) {
            chain.has_target=false;
            chain.current_weight=0.f;
        }
        reports_.push_back(report);
    }
}
void ProceduralIkController::reset() noexcept {
    for(auto& c:chains_) {
        c.submitted=false;c.has_target=false;c.current_weight=0.f;
        c.priority=0;
    }
    reports_.clear();
}
}
