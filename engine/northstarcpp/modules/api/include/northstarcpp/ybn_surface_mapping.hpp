#pragma once
#include <northstarcpp/physics_surface.hpp>
#include <cstdint>
namespace northstarcpp::ybn_surface_mapping {
inline constexpr std::uint32_t gta5(std::uint32_t id) noexcept {
    using namespace physics_surface;
    switch(id) {
        case 1: case 2: case 3: case 4: case 5: case 6:
        case 7: case 8: case 9: case 10: case 11: case 12:
        case 13: case 14: case 15: case 16: case 17:
            return stone; // concrete/tarmac/rock/cobblestone/stone masonry
        case 18: case 19: case 20: case 21: case 22: case 23: case 24:
            return dirt; // sand variants
        case 25: case 26: case 27: case 28: case 29: case 30:
            return snow; // ice and snow
        default: return unknown; // source ID is always retained separately
    }
}
} // namespace northstarcpp::ybn_surface_mapping
