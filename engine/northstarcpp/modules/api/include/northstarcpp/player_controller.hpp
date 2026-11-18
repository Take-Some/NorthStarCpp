// NorthStar PlayerFramework v1: controller lifetime is independent of Pawn.
// Stable authored Entity IDs, not transient Flecs handles or physics indices.
#pragma once
#include <northstarcpp/player_character_components.hpp>
#include <northstarcpp/gameplay_possession_state.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/player_character_motor.hpp>
#include <northstarcpp/player_camera_rig.hpp>
#include <northstarcpp/camera.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace northstarcpp::gameplay {
inline std::optional<std::size_t> entity_index(Scene const& scene,
    std::uint64_t stable_id) noexcept {
    if(!stable_id)return std::nullopt;
    for(std::size_t i=0;i<scene.objects.size();++i)
        if(scene.objects[i].node.id==stable_id)return i;
    return std::nullopt;
}
inline std::optional<SceneComponentBlob const*> component(
    Scene const& scene,std::uint64_t id,char const* type) {
    for(auto const& c:scene.components)
        if(c.entity_id==id && c.type==type &&
           c.version==player_character::version)return &c;
    return std::nullopt;
}
class PlayerController final {
public:
    // No default Pawn means the controller is alive but unpossessed.
    bool possessed() const noexcept {return binding_.possessed();}
    std::uint64_t controlled_entity() const noexcept {return binding_.controlled_entity();}
    std::uint64_t possession_revision() const noexcept {return binding_.revision();}
    std::optional<PossessionEvent> poll_possession_event() noexcept {
        return binding_.take_event();
    }
    std::uint64_t dropped_possession_events() const noexcept {
        return binding_.overwritten_events();
    }
    std::optional<std::size_t> controlled_index(Scene const& scene) const noexcept {
        return binding_.controlled_index(scene);
    }
    // An explicit camera target is independent of Pawn possession. Without
    // an override the camera follows the possessed Pawn. Cinematic and
    // spectator views can therefore leave the controlled Pawn untouched.
    bool has_view_target_override() const noexcept {return view_target_id_!=0;}
    std::uint64_t view_target() const noexcept {
        return view_target_id_ ? view_target_id_ : binding_.controlled_entity();
    }
    std::optional<std::size_t> view_target_index(Scene const& scene) const noexcept {
        return entity_index(scene,view_target());
    }
    void set_view_target(Scene const& scene,std::uint64_t id) {
        if(id && !entity_index(scene,id))
            throw std::invalid_argument("View target entity missing");
        view_target_id_=id;
    }
    void reset_view_target() noexcept {view_target_id_=0;}
    // Does not mutate scene or physics. Caller commits selection to physics
    // only after the receiving provider has acknowledged the handoff.
    bool possess(Scene const& scene,std::uint64_t id) {
        return binding_.possess(scene,id);
    }
    void unpossess() noexcept { (void)binding_.unpossess(); }
    void reconcile(Scene const& scene) noexcept {
        // A destroyed Pawn invalidates possession, not the Controller itself.
        (void)binding_.reconcile(scene);
        if(view_target_id_ && !entity_index(scene,view_target_id_))
            reset_view_target();
    }
    bool auto_possess(Scene const& scene) {
        auto const startup=scene_pawn::startup_pawn(scene);
        if(startup.conflict==scene_pawn::StartupConflict::Controllers)
            throw std::invalid_argument("Multiple active PlayerController entities");
        if(startup.conflict==scene_pawn::StartupConflict::Pawns)
            throw std::invalid_argument("Multiple local auto-possess Pawns");
        if(!startup.id) {
            unpossess();
            return false;
        }
        possess(scene,startup.id);
        return true;
    }
private:
    PossessionState binding_;
    std::uint64_t view_target_id_=0;
};
} // namespace northstarcpp::gameplay
