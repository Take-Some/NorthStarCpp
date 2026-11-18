// Stable Pawn target validation shared by local Player and AI controllers.
#pragma once
#include <northstarcpp/player_character_components.hpp>
#include <northstarcpp/scene.hpp>
#include <cstdint>
namespace northstarcpp::gameplay {
inline bool valid_controller_pawn(Scene const& scene,std::uint64_t id) noexcept {
    if(!id || !player_character::has_pawn(scene,id))return false;
    for(auto const& object:scene.objects)
        if(object.node.id==id)return true;
    return false;
}
} // namespace northstarcpp::gameplay
