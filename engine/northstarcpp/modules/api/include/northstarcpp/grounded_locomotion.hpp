#pragma once
// Bounded, actor-owned terrain adaptation. Probes are executed by PhysicsScene
// after a simulation step; the animation frame only queues and consumes them.
#include <northstarcpp/procedural_ik.hpp>
#include <northstarcpp/physics.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace northstarcpp {
struct GroundedFootDefinition {
    std::string chain,ankle,toe_bone;
    math::float3 heel_offset{0.f,-.04f,-.11f};
    math::float3 toe_offset{0.f,-.04f,.16f};
    float clearance=.015f,max_raise=.20f,max_drop=.25f;
};
struct GroundedBodyDefinition {
    std::string pelvis,torso;
    float max_pelvis_drop=.12f,max_torso_tilt=.10f;
};
struct GroundedFootContact {
    std::optional<PhysicsGroundProbe> heel,toe;
};
struct GroundedFootReport {
    std::string_view chain;
    bool planted=false,heel_hit=false,toe_hit=false;
    float target_adjustment=0.f,anchor_slip_meters=0.f;
    math::float3 anchor_world{};
    std::uint32_t support_body=UINT32_MAX;
};
class GroundedLocomotionController {
public:
    GroundedLocomotionController(Skeleton const&,
        std::vector<GroundedFootDefinition>,GroundedBodyDefinition);
    void reset() noexcept;
    bool provide_contact(std::string_view chain,GroundedFootContact contact);
    bool set_foot_phase(std::string_view chain,bool planting) noexcept;
    void finish_toes(SkeletonPose& pose);
    void update(PhysicsScene const* physics,std::size_t ignored_body,
        SkeletonPose& pose,ProceduralIkController& ik,
        math::float4x4 const& rig_to_world,float dt,
        std::uint32_t query_channel=2);
    std::vector<GroundedFootReport> const& reports() const noexcept {
        return reports_;
    }
    float pelvis_offset() const noexcept {return pelvis_offset_;}
    std::size_t foot_count() const noexcept {return feet_.size();}
private:
    struct Foot {
        GroundedFootDefinition def;
        std::size_t ankle=0,toe=0;
        std::uint64_t heel_ticket=0,toe_ticket=0;
        GroundedFootContact contacts;
        math::float3 last_world{},locked_world{},locked_platform_local{};
        std::uint32_t locked_platform=UINT32_MAX;
        float toe_pitch=0.f;
        bool phase_explicit=false,phase_planted=false;
        bool has_last=false,planted=false,has_contact=false;
        float time_without_contact=0.f,plant_age=0.f,contact_age=0.f;
    };
    Skeleton const* rig_;
    GroundedBodyDefinition body_;
    std::size_t pelvis_=0,torso_=0;
    std::vector<Foot> feet_;
    std::vector<GroundedFootReport> reports_;
    float pelvis_offset_=0.f,torso_tilt_=0.f;
};
}
