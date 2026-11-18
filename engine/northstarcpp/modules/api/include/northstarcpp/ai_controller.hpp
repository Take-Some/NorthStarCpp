// NorthStar AI Controller — scene-stable possession and movement intent.
// Intentionally provider-neutral: applying intent belongs to CharacterMotor/Jolt.
#pragma once
#include <northstarcpp/gameplay_possession_state.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace northstarcpp::gameplay {
class AIController final {
public:
    struct MoveIntent {
        Vector3 direction{0.f,0.f,0.f};
        float distance=0.f;
        bool arrived=true;
    };
    std::uint64_t controlled_entity() const noexcept {return binding_.controlled_entity();}
    std::uint64_t possession_revision() const noexcept {return binding_.revision();}
    std::optional<PossessionEvent> poll_possession_event() noexcept {
        return binding_.take_event();
    }
    std::uint64_t dropped_possession_events() const noexcept {
        return binding_.overwritten_events();
    }
    bool possessed() const noexcept {return binding_.possessed();}
    void possess(Scene const& scene,std::uint64_t pawn) {
        if(binding_.possess(scene,pawn))stop();
    }
    void unpossess() noexcept { (void)binding_.unpossess();stop(); }
    void reconcile(Scene const& scene) noexcept {
        if(binding_.reconcile(scene))stop();
    }
    // Returns true only when navigation must replan. Repeated identical
    // moveTo calls must not discard in-progress NavMesh raster or waypoints.
    bool move_to(Vector3 goal,float stop_radius=.4f) {
        if(!possessed())throw std::logic_error("AIController is not possessed");
        if(!std::isfinite(goal.x)||!std::isfinite(goal.y)||
           !std::isfinite(goal.z)||!std::isfinite(stop_radius)||
           stop_radius<0.f||stop_radius>1000.f)
            throw std::invalid_argument("Invalid AI movement goal");
        auto const changed=!has_goal_ ||
            std::hypot(goal_.x-goal.x,goal_.y-goal.y,goal_.z-goal.z)>.01f ||
            std::abs(stop_radius_-stop_radius)>.001f;
        if(!changed)return false;
        goal_=goal;
        stop_radius_=stop_radius;
        has_goal_=true;
        return true;
    }
    void stop() noexcept {has_goal_=false;}
    bool has_goal() const noexcept {return has_goal_;}
    Vector3 goal() const noexcept {return goal_;}
    float stop_radius() const noexcept {return stop_radius_;}
    MoveIntent next_intent(Scene const& scene) noexcept {
        MoveIntent result{};
        if(!possessed() || !has_goal_)return result;
        auto actor=scene.objects.end();
        for(auto it=scene.objects.begin();it!=scene.objects.end();++it)
            if(it->node.id==controlled_entity()) {actor=it;break;}
        if(actor==scene.objects.end() || !valid_controller_pawn(scene,controlled_entity())) {
            unpossess();return result;
        }
        auto const dx=goal_.x-actor->position.x;
        auto const dz=goal_.z-actor->position.z;
        const float horizontal=std::hypot(dx,dz);
        const float vertical=std::abs(goal_.y-actor->position.y);
        result.distance=horizontal+vertical;
        if(horizontal<=stop_radius_ &&
           vertical<=std::max(stop_radius_,.6f)) {
            stop();
            return result;
        }
        // A vertical-only goal needs the NavMesh to find the next ramp or
        // staircase waypoint. Never divide by zero or move vertically by
        // directly altering a physical Pawn transform.
        if(horizontal>.00001f)
            result.direction={dx/horizontal,0.f,dz/horizontal};
        result.arrived=false;
        return result;
    }
private:
    PossessionState binding_;
    Vector3 goal_{0.f,0.f,0.f};
    float stop_radius_=.4f;
    bool has_goal_=false;
};
} // namespace northstarcpp::gameplay
