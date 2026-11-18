// Canonical Pawn component queries: no SceneObject::player flag.
#pragma once
#include <northstarcpp/player_blueprint_controller.hpp>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <stdexcept>
namespace northstarcpp::scene_pawn {
inline bool is_pawn(Scene const& scene,std::uint64_t id) noexcept {
    return player_character::has_pawn(scene,id);
}
inline bool is_pawn(Scene const& scene,SceneObject const& object) noexcept {
    return is_pawn(scene,object.node.id);
}
enum class StartupConflict { None, Controllers, Pawns };
struct StartupPawn {
    std::uint64_t id=0;
    StartupConflict conflict=StartupConflict::None;
};

inline bool legacy_auto_possess(SceneComponentBlob const& record) noexcept {
    return record.type==player_character::pawn_type &&
        player_character::detail::pawn_valid(nullptr,record.version,
            record.payload.data(),record.payload.size()) &&
        player_character::detail::get32(record.payload.data(),4)!=0;
}

// Explicit controller settings take precedence, including an explicit opt-out.
// Scenes authored before controller entities use the Pawn's compatibility flag.
inline StartupPawn startup_pawn(Scene const& scene) noexcept {
    std::uint64_t candidate=0;
    for(auto const& record:scene.components) {
        if(record.type!=player_blueprint::controller_type ||
           record.version!=player_blueprint::version)continue;
        auto const binding=player_blueprint::deserialize_controller(record.payload);
        if(!binding || !binding->second)continue;
        if(candidate && candidate!=binding->first)
            return {0,StartupConflict::Controllers};
        candidate=binding->first;
    }
    if(candidate)return {candidate};

    for(auto const& record:scene.components) {
        if(!record.entity_id || !legacy_auto_possess(record) ||
           player_blueprint::controller_for_pawn(scene,record.entity_id))continue;
        if(candidate && candidate!=record.entity_id)
            return {0,StartupConflict::Pawns};
        candidate=record.entity_id;
    }
    return {candidate};
}

inline bool is_local(Scene const& scene,std::uint64_t id) noexcept {
    return id && is_pawn(scene,id) && startup_pawn(scene).id==id;
}
inline bool is_local(Scene const& scene,SceneObject const& obj) noexcept {
    return is_local(scene,obj.node.id);
}
inline std::optional<std::size_t> local_index(Scene const& scene) noexcept {
    auto const startup=startup_pawn(scene);
    if(!startup.id || !is_pawn(scene,startup.id))return std::nullopt;
    for(std::size_t i=0;i<scene.objects.size();++i)
        if(scene.objects[i].node.id==startup.id)return i;
    return std::nullopt;
}

// Change the authored startup choice as one edit. Keep the compatibility bit
// in sync while preserving the Pawn kind and its input configuration.
inline void set_startup_pawn(Scene& scene,std::uint64_t id,bool enabled) {
    if(!id || !is_pawn(scene,id))
        throw std::invalid_argument("Startup player must be a Pawn");
    auto const binding=player_blueprint::controller_for_pawn(scene,id);
    for(auto& record:scene.components) {
        if(record.type==player_blueprint::controller_type &&
           record.version==player_blueprint::version) {
            auto const current=player_blueprint::deserialize_controller(record.payload);
            if(current && (enabled || current->first==id))
                record.payload[8]=enabled && binding &&
                    record.entity_id==binding->entity_id ? 1 : 0;
        } else if(record.type==player_character::pawn_type &&
                  (enabled || record.entity_id==id) &&
                  player_character::detail::pawn_valid(nullptr,record.version,
                      record.payload.data(),record.payload.size())) {
            record.payload[4]=enabled && record.entity_id==id ? 1 : 0;
        }
    }
}

// Import-only migration for pre-controller scene formats.
inline void migrate_imported_local(Scene& scene,std::uint64_t id) {
    if(!id)throw std::invalid_argument("Imported Pawn requires an entity ID");
    for(auto& c:scene.components)
        if(c.entity_id==id && c.type==player_character::pawn_type) {
            if(legacy_auto_possess(c))return;
            c.version=player_character::version;
            c.payload.clear();
            player_character::detail::push32(c.payload,1);
            player_character::detail::push32(c.payload,1);
            player_character::detail::push32(c.payload,1);
        }
    player_character::add_character_components(scene,id,true);
}
} // namespace northstarcpp::scene_pawn
