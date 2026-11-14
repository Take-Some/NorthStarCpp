// NorthStarCpp animation transition curve shared by scene and skeletal playback.
#pragma once
#include <algorithm>
#include <cmath>

namespace northstarcpp::animation {

inline float fermi_dirac_weight(float elapsed, float duration, float epsilon)
{
    auto const width = duration / (2.f * std::log(1.f / epsilon + 1.f));
    auto const magnitude = 1.f + 2.f * epsilon;
    auto const value = magnitude / (1.f + std::exp(-(elapsed - duration / 2.f) / width));
    return std::clamp(value - epsilon, 0.f, 1.f);
}

} // namespace northstarcpp::animation
