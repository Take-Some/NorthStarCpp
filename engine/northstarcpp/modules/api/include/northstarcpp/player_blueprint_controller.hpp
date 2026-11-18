#pragma once
#include <northstarcpp/player_blueprint_core.hpp>

namespace northstarcpp::player_blueprint {
inline std::string serialize_controller(std::uint64_t pawn,bool local) {
    if(!pawn)throw std::invalid_argument("PlayerController must reference Pawn");
    std::string bytes;
    player_character::detail::push32(bytes,std::uint32_t(pawn));
    player_character::detail::push32(bytes,std::uint32_t(pawn>>32));
    player_character::detail::push32(bytes,local?1u:0u);
    return bytes;
}
inline std::optional<std::pair<std::uint64_t,bool>>
deserialize_controller(std::string const& bytes) {
    if(bytes.size()!=12)return std::nullopt;
    auto* p=bytes.data();
    auto id=std::uint64_t(player_character::detail::get32(p,0))|
        (std::uint64_t(player_character::detail::get32(p,4))<<32);
    auto flag=player_character::detail::get32(p,8);
    if(!id||flag>1)return std::nullopt;
    return std::make_pair(id,flag!=0);
}

inline SceneComponentBlob const* controller_for_pawn(
    Scene const& scene,std::uint64_t pawn) noexcept {
    SceneComponentBlob const* inactive=nullptr;
    for(auto const& record:scene.components) {
        if(record.type!=controller_type || record.version!=version)continue;
        auto const binding=deserialize_controller(record.payload);
        if(!binding || binding->first!=pawn)continue;
        if(binding->second)return &record;
        if(!inactive)inactive=&record;
    }
    return inactive;
}
}
