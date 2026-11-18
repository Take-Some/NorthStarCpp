#include <northstarcpp/posture_ik.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace northstarcpp {
namespace {
using math::float3;
bool good(float3 const& v) {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
}
float3 origin(math::float4x4 const& m) {
    return {m.elements[3][0],m.elements[3][1],m.elements[3][2]};
}
float3 unit(float3 v,float3 fallback={0,0,1}) {
    auto n=v.magnitude();
    return n>.0001f?v/n:fallback;
}
}
DistributedLookAt::DistributedLookAt(Skeleton const& rig,
    std::vector<LookAtJoint> joints):rig_(&rig) {
    if(joints.empty()||joints.size()>8)
        throw std::invalid_argument("Look-at requires 1..8 joints");
    joints_.reserve(joints.size());
    for(auto& definition:joints) {
        auto index=rig.joint_index(definition.bone);
        if(index>=rig.joints.size()||
           !good(definition.forward)||definition.forward.magnitude()<.5f||
           !std::isfinite(definition.influence)||
           !std::isfinite(definition.max_radians)||
           definition.influence<0.f||definition.influence>1.f||
           definition.max_radians<=0.f||definition.max_radians>.20f)
            throw std::invalid_argument("Invalid Look-at joint");
        for(auto const& prior:joints_)
            if(prior.index==index)
                throw std::invalid_argument("Duplicate Look-at bone");
        joints_.push_back({std::move(definition),index});
    }
}
bool DistributedLookAt::set_target(float3 point,float weight) {
    if(!good(point)||!std::isfinite(weight)||weight<0.f||weight>1.f)
        return false;
    target_world_=point;
    target_weight_=weight;
    return true;
}
void DistributedLookAt::apply(SkeletonPose& pose,
    math::float4x4 const& rig_to_world,float dt) {
    if(!std::isfinite(dt)||dt<0.f||dt>.25f ||
       pose.joints_world().size()!=rig_->joints.size())return;
    math::float4x4 inverse;
    if(!inverse.try_invert(rig_to_world))return;
    weight_+=(target_weight_-weight_)*
        (1.f-std::exp(-dt/.11f));
    if(weight_<.001f)return;
    auto const target=inverse.transform_position(target_world_);
    for(auto const& rule:joints_) {
        auto const& transform=pose.joints_world()[rule.index];
        auto const forward=unit(transform.transform_direction(
            rule.data.forward));
        auto toward=target-origin(transform);
        if(toward.magnitude()<.01f)continue;
        toward=unit(toward);
        auto axis=math::cross_product(toward,forward); // row-vector FK
        float const sine=axis.magnitude();
        auto const cosine=std::clamp(math::dot_product(toward,forward),
            -1.f,1.f);
        if(sine<.00001f) {
            if(cosine>0.f)continue;
            axis=math::cross_product(float3{0,1,0},forward);
            if(axis.magnitude()<.00001f)axis={1,0,0};
        }
        auto const radians=std::min(
            std::atan2(sine,cosine)*rule.data.influence*weight_,
            rule.data.max_radians);
        if(radians<.00001f)continue;
        auto const parent=rig_->joints[rule.index].parent;
        if(parent>=0) {
            math::float4x4 inverse_parent;
            if(!inverse_parent.try_invert(
                pose.joints_world()[std::size_t(parent)]))continue;
            axis=inverse_parent.transform_direction(axis);
        }
        pose.adjust_local_joint(*rig_,rule.data.bone,
            {0,0,0},unit(axis,{0,1,0}),radians);
    }
}
std::size_t VehicleSeatIk::submit(Skeleton const& rig,
    SkeletonPose const& pose,ProceduralIkController& ik,
    math::float4x4 const& world_to_rig,
    NorthStarVehicleSeatState const& seat,
    std::vector<VehicleSeatReach> const& targets) {
    if(!seat.occupant ||seat.size<sizeof(NorthStarVehicleSeatState)||
       targets.size()>8 ||
       !std::isfinite(seat.world_yaw)||
       !std::isfinite(seat.world_position[0])||
       !std::isfinite(seat.world_position[1])||
       !std::isfinite(seat.world_position[2]))return 0;
    auto const c=std::cos(seat.world_yaw),s=std::sin(seat.world_yaw);
    std::size_t count=0;
    for(auto const& reach:targets) {
        if(!good(reach.seat_local_target)||
           !std::isfinite(reach.weight)||reach.weight<=0.f||
           reach.weight>1.f)continue;
        auto const elbow=rig.joint_index(reach.knee_or_elbow);
        if(elbow>=rig.joints.size()||
           elbow>=pose.joints_world().size())continue;
        auto const& local=reach.seat_local_target;
        float3 world{
            seat.world_position[0]+c*local.x+s*local.z,
            seat.world_position[1]+local.y,
            seat.world_position[2]-s*local.x+c*local.z};
        IkTargetRequest goal;
        goal.position=world_to_rig.transform_position(world);
        goal.pole=origin(pose.joints_world()[elbow]);
        goal.weight=reach.weight;
        goal.priority=reach.priority;
        if(ik.submit(reach.chain,goal))++count;
    }
    return count;
}
}
