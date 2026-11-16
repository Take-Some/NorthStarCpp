// NorthStar PlayerFramework v1: controller lifetime is independent of Pawn.
// Stable authored Entity IDs, not transient Flecs handles or physics indices.
#pragma once
#include <northstarcpp/player_character_components.hpp>
#include <northstarcpp/player_blueprint_controller.hpp>
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
    bool possessed() const noexcept {return pawn_id_!=0;}
    std::uint64_t controlled_entity() const noexcept {return pawn_id_;}
    std::optional<std::size_t> controlled_index(Scene const& scene) const noexcept {
        return entity_index(scene,pawn_id_);
    }
    // Does not mutate scene or physics. Caller commits selection to physics
    // only after the receiving provider has acknowledged the handoff.
    void possess(Scene const& scene,std::uint64_t id) {
        auto index=entity_index(scene,id);
        if(!index)
            throw std::invalid_argument("Pawn entity missing");
        // Visual visibility is not possession state. Blueprint Pawns hide
        // their logical capsule but remain physically controllable.
        if(!player_character::has_pawn(scene,id))
            throw std::invalid_argument("Entity is not a Pawn");
        pawn_id_=id;
    }
    void unpossess() noexcept {pawn_id_=0;}
    void reconcile(Scene const& scene) noexcept {
        if(!controlled_index(scene))unpossess();
    }
    // Auto-possess is component metadata; there is no SceneObject fallback.
    // Ambiguous explicit auto-possess is an authoring error, not iteration order.
    bool auto_possess(Scene const& scene) {
        std::uint64_t candidate=0;
        // New Controller Entity owns possession, never the Pawn visual.
        for(auto const& record:scene.components) {
            if(record.type!=player_blueprint::controller_type ||
               record.version!=player_blueprint::version)continue;
            auto binding=player_blueprint::deserialize_controller(record.payload);
            if(!binding || !binding->second)continue;
            if(candidate && candidate!=binding->first)
                throw std::invalid_argument("Multiple active PlayerController entities");
            candidate=binding->first;
        }
        if(candidate) {
            possess(scene,candidate);
            return true;
        }
        for(auto const& item:scene.components) {
            if(item.type!=player_character::pawn_type ||
               item.entity_id==0 || item.version!=player_character::version ||
               !player_character::detail::pawn_valid(nullptr,item.version,
                   item.payload.data(),item.payload.size()) ||
               !player_character::detail::get32(item.payload.data(),4))
                continue;
            if(candidate && candidate!=item.entity_id)
                throw std::invalid_argument("Multiple local auto-possess Pawns");
            candidate=item.entity_id;
        }
        if(!candidate) {unpossess();return false;}
        possess(scene,candidate);
        return true;
    }
private:
    std::uint64_t pawn_id_=0;
};
} // namespace northstarcpp::gameplay
