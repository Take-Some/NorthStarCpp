#pragma once
// Backend-neutral FP16 linear HDR temporal AA acceptance algorithm.
// Mirrors the intended GPU reprojection+history rejection contract,
// without owning GPU images or modifying the engine.render ABI.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace northstarcpp::render {
struct TaaSettings {
    float history_weight=.90f;       // stationary history contribution
    float variance_gamma=1.25f;      // neighborhood AABB sigma expansion
    float depth_relative_limit=.02f; // reject disoccluded history
    float velocity_scale=.045f;      // reduce history for fast motion
};
struct TaaSample {
    std::array<float,3> color{};
    float depth=0.f;                // linear view-space depth, not device Z
};
struct TaaBlend {
    std::array<float,3> hdr{};
    float history_fraction=0.f;
    bool rejected=false;
};
inline TaaBlend taa_resolve_reference(
    std::array<TaaSample,9> const& neighborhood,
    TaaSample const& previous,
    float previous_sample_depth,
    std::array<float,2> const& motion_pixels,
    bool history_valid,
    TaaSettings settings={}) noexcept {
    TaaBlend result{neighborhood[4].color,0.f,!history_valid};
    auto const& central=neighborhood[4];
    auto valid=[](float value) {
        return std::isfinite(value)&&value>=0.f&&value<=65504.f;
    };
    if(!history_valid || !valid(central.depth) || central.depth<=0.f ||
       !valid(previous_sample_depth) || previous_sample_depth<=0.f ||
       !std::isfinite(motion_pixels[0])||!std::isfinite(motion_pixels[1])) {
        result.rejected=true;return result;
    }
    const float relative_depth=std::abs(
        previous_sample_depth-central.depth)/
        std::max(central.depth,.001f);
    if(relative_depth>std::clamp(settings.depth_relative_limit,.0001f,1.f)) {
        result.rejected=true;return result;
    }
    std::array<float,3> minimum{},maximum{},mean{},variance{};
    for(unsigned channel=0;channel<3u;++channel) {
        minimum[channel]=std::numeric_limits<float>::infinity();
        maximum[channel]=0.f;
        for(auto const& p:neighborhood) {
            if(!valid(p.color[channel])) {
                result.rejected=true;return result;
            }
            const auto value=p.color[channel];
            minimum[channel]=std::min(minimum[channel],value);
            maximum[channel]=std::max(maximum[channel],value);
            mean[channel]+=value/9.f;
            variance[channel]+=value*value/9.f;
        }
        variance[channel]=std::sqrt(std::max(
            0.f,variance[channel]-mean[channel]*mean[channel]));
    }
    const float velocity=std::hypot(
        motion_pixels[0],motion_pixels[1]);
    float history_fraction=std::clamp(settings.history_weight,0.f,.99f)/
        (1.f+velocity*std::max(0.f,settings.velocity_scale));
    for(unsigned channel=0;channel<3u;++channel) {
        if(!valid(previous.color[channel])) {
            result.rejected=true;return result;
        }
        const float delta=std::max(0.f,settings.variance_gamma)*
                          variance[channel];
        const float lo=std::max(minimum[channel],mean[channel]-delta);
        const float hi=std::min(maximum[channel],mean[channel]+delta);
        const auto clipped=std::clamp(previous.color[channel],
                                      std::min(lo,hi),std::max(lo,hi));
        result.hdr[channel]=
            central.color[channel]*(1.f-history_fraction)+
            clipped*history_fraction;
    }
    result.history_fraction=history_fraction;
    result.rejected=false;
    return result;
}
} // namespace northstarcpp::render
