#pragma once
// NorthStar original opt-in exponential height fog. Not volumetric light
// scattering: this is analytic per-pixel extinction in deferred lighting.
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
namespace northstarcpp::render {
inline float height_fog_density_from_environment() noexcept {
    auto const* value=std::getenv("NORTHSTARCPP_HEIGHT_FOG_DENSITY");
    if(!value || !*value)return 0.f;
    char* end=nullptr;
    errno=0;
    float const parsed=std::strtof(value,&end);
    // Reject invalid/overflow/out-of-range input without injecting NaN into
    // GPU constants. 0 disables the effect and exactly preserves baseline.
    if(end==value || !end || *end!='\0' || errno==ERANGE ||
       !std::isfinite(parsed) || parsed<0.f || parsed>.25f)return 0.f;
    return parsed;
}
inline float height_fog_extinction(float world_distance,
                                   float eye_height,float pixel_height,
                                   float density,float height_falloff=.025f) noexcept {
    if(!(world_distance>0.f) || !(density>0.f))return 0.f;
    float a=std::exp(std::clamp(-height_falloff*eye_height,-8.f,8.f));
    float b=std::exp(std::clamp(-height_falloff*pixel_height,-8.f,8.f));
    auto optical_depth=density*world_distance*
        std::clamp(.5f*(a+b),.01f,8.f);
    return std::clamp(1.f-std::exp(-std::min(optical_depth,20.f)),0.f,.96f);
}
}
