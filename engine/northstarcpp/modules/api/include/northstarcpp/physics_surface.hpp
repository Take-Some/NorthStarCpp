// Provider-neutral physics material identifiers used by footsteps and collision.
// Material IDs are stable across .nscn serialization and native physics providers.
#pragma once
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace northstarcpp::physics_surface {
inline constexpr std::uint32_t unknown=0;
inline constexpr std::uint32_t dirt=1;
inline constexpr std::uint32_t grass=2;
inline constexpr std::uint32_t metal=3;
inline constexpr std::uint32_t mud=4;
inline constexpr std::uint32_t snow=5;
inline constexpr std::uint32_t stone=6;
inline constexpr std::uint32_t water=7;
inline constexpr std::uint32_t wood=8;
inline constexpr std::uint32_t maximum=wood;

inline std::string_view name(std::uint32_t id) noexcept {
    constexpr std::array<std::string_view,9> labels{
        "unknown","dirt","grass","metal","mud","snow",
        "stone","water","wood"};
    return id<labels.size()?labels[id]:labels[0];
}

// Material names may originate from OBJ groups or native YDD/YDR draw sections.
// Unknown materials return 0 so the caller can fall back to an authored
// collider-level surface tag; do not silently classify all render materials.
inline std::uint32_t from_material_reference(std::string_view text) {
    std::string lower;
    lower.reserve(text.size());
    for(unsigned char c:text)
        lower.push_back(static_cast<char>(std::tolower(c)));
    auto token=[&](std::string_view name) {
        std::size_t found=0;
        while((found=lower.find(name,found))!=std::string::npos) {
            auto const before=found==0 || !std::isalnum(
                static_cast<unsigned char>(lower[found-1]));
            auto const at=found+name.size();
            auto const after=at==lower.size() || !std::isalnum(
                static_cast<unsigned char>(lower[at]));
            if(before && after)return true;
            ++found;
        }
        return false;
    };
    if(token("grass"))return grass;
    if(token("wood")||token("timber"))return wood;
    if(token("metal")||token("steel")||token("iron"))return metal;
    if(token("mud"))return mud;
    if(token("snow"))return snow;
    if(token("water"))return water;
    if(token("dirt")||token("soil")||token("sand"))return dirt;
    if(token("stone")||token("concrete")||token("asphalt")||
       token("road")||token("rock"))return stone;
    return unknown;
}
} // namespace northstarcpp::physics_surface
