#pragma once
// NorthStarCpp procedural IK post-process.
// Targets are explicitly expressed in skeleton-local space; no Abby-specific
// bone names or global physics assumptions are hidden in the runtime.
#include <northstarcpp/skeleton.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace northstarcpp {

struct IkChainDefinition {
    std::string name, root, hinge, effector;
    float root_angle_limit=1.55f;
    float hinge_angle_limit=1.80f;
    float blend_in_seconds=.10f;
    float blend_out_seconds=.16f;
};
struct IkTargetRequest {
    math::float3 position{},pole{};
    float weight=1.f;                 // [0,1]
    int priority=0;                   // larger wins; stable order on tie
    bool align_effector=false;
    math::float3 up_normal{0.f,1.f,0.f};
    float max_tilt_radians=.45f;
};
struct IkGroundContact {
    bool hit=false;
    math::float3 point{},normal{0.f,1.f,0.f};  // skeleton-local
    float foot_clearance=0.f;
    float max_raise=.25f,max_drop=.30f;
};
struct IkSolveReport {
    std::string_view chain;
    bool attempted=false,solved=false;
    float effective_weight=0.f;
    float residual_meters=0.f;       // final effector-to-goal distance
};
// One actor owns one controller. No VFS, physics or ECS access occurs here.
// The caller supplies non-blocking ground-probe results before each update.
class ProceduralIkController {
public:
    explicit ProceduralIkController(Skeleton const&);
    bool register_chain(IkChainDefinition definition);
    bool submit(std::string_view chain,IkTargetRequest request);
    bool submit_ground(std::string_view chain,IkGroundContact const& contact,
                       IkTargetRequest request, SkeletonPose const& pose);
    void evaluate(SkeletonPose& pose,float delta_seconds,
                  std::size_t max_solvers=8);
    void reset() noexcept;
    std::vector<IkSolveReport> const& reports() const noexcept {return reports_;}
    std::size_t chain_count() const noexcept {return chains_.size();}
private:
    struct Chain {
        IkChainDefinition definition;
        std::size_t root=0,hinge=0,effector=0;
        IkTargetRequest pending{},current{};
        float current_weight=0.f;
        int priority=0;
        bool submitted=false,has_target=false;
    };
    Skeleton const* rig_;
    std::vector<Chain> chains_;
    std::vector<std::size_t> order_;
    std::vector<IkSolveReport> reports_;
};
} // namespace northstarcpp
