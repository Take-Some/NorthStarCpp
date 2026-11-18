#pragma once
// Post-animation optional look-at and vehicle-relative reach. Rig-authored
// joint names and target coordinates, never game-character hardcoding.
#include <northstarcpp/procedural_ik.hpp>
#include <northstarcpp/vehicle_dynamics_plugin.h>
#include <vector>
#include <string>
namespace northstarcpp {
struct LookAtJoint {
    std::string bone;
    float influence=1.f,max_radians=.12f;
    math::float3 forward{0,0,1}; // authored per skeleton
};
class DistributedLookAt {
public:
    DistributedLookAt(Skeleton const&,std::vector<LookAtJoint> joints);
    bool set_target(math::float3 world,float weight);
    void apply(SkeletonPose&,math::float4x4 const& rig_to_world,float dt);
    void reset() noexcept {weight_=0.f;target_weight_=0.f;}
    void disable() noexcept {target_weight_=0.f;}
    std::size_t joint_count() const noexcept {return joints_.size();}
private:
    struct Joint {LookAtJoint data;std::size_t index;};
    Skeleton const* rig_;
    std::vector<Joint> joints_;
    math::float3 target_world_{};
    float target_weight_=0.f,weight_=0.f;
};
struct VehicleSeatReach {
    std::string chain,knee_or_elbow;
    math::float3 seat_local_target{};
    float weight=1.f;
    int priority=40;
};
class VehicleSeatIk {
public:
    // The VehicleService's occupant/seat lifecycle is authoritative. Call
    // only while the character is occupying the chosen seat.
    static std::size_t submit(
        Skeleton const&,SkeletonPose const&,ProceduralIkController&,
        math::float4x4 const& world_to_rig,
        NorthStarVehicleSeatState const& seat,
        std::vector<VehicleSeatReach> const&);
};
}
