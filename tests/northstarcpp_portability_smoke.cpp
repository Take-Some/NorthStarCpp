// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

// NorthStarCpp engine identity + compatibility test for the NorthStarCpp math API.
#include <cstdint>
#include <iostream>
#include <northstarcpp/identity.hpp>
using u32 = std::uint32_t;
using u64 = std::uint64_t;
#include <northstarcpp/compat/math_randoms_generator.h>
int main() {
    static_assert(northstarcpp::engine_name == "NorthStarCpp");
    northstarcpp::math::random32 random(1337U);
    const u32 result = random.random(100U);
    if (result >= 100U ||
        random.seed() != (u32(0x08088405U) * u32(1337U) + u32(1U)))
        return 1;
    random.seed(0U);
    if (random.random(0U) != 0U || random.seed() != 1U)
        return 2;
    std::cout << northstarcpp::engine_name << " GCC portability smoke: PASS\n";
    return 0;
}
