// Canonical Pawn component queries: no SceneObject::player flag.
#pragma once
#include <northstarcpp/player_character_components.hpp>
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
inline bool is_local(Scene const& scene,std::uint64_t id) noexcept {
    for(auto const& c:scene.components)
        if(c.entity_id==id && c.type==player_character::pawn_type &&
           c.version==player_character::version &&
           player_character::detail::pawn_valid(nullptr,c.version,c.payload.data(),c.payload.size()))
            return player_character::detail::get32(c.payload.data(),4)!=0;
    return false;
}
inline bool is_local(Scene const& scene,SceneObject const& obj) noexcept {
    return is_local(scene,obj.node.id);
}
inline std::optional<std::size_t> local_index(Scene const& scene) noexcept {
    for(std::size_t i=0;i<scene.objects.size();++i)
        if(is_local(scene,scene.objects[i]))return i;
    return std::nullopt;
}
// Import-only migration: serializable Pawn component is the sole source of truth.
inline void migrate_imported_local(Scene& scene,std::uint64_t id) {
    if(!id)throw std::invalid_argument("Imported Pawn requires an entity ID");
    if(is_local(scene,id))return;
    for(auto& c:scene.components)
        if(c.entity_id==id && c.type==player_character::pawn_type) {
            c.version=player_character::version;
            c.payload.clear();
            player_character::detail::push32(c.payload,1);
            player_character::detail::push32(c.payload,1);
            player_character::detail::push32(c.payload,1);
        }
    player_character::add_character_components(scene,id,true);
}
} // namespace northstarcpp::scene_pawn
