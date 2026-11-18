#include "../engine/northstarcpp/modules/render/shared/temporal_taa.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
int main() {
    using namespace northstarcpp::render;
    std::array<TaaSample,9> region{};
    for(auto& p:region) {
        p.color={2.f,1.f,.5f}; // FP16-range linear HDR, not tonemapped
        p.depth=8.f;
    }
    TaaSample previous{};
    previous.color={2.1f,1.1f,.5f};
    previous.depth=8.f;
    auto reset=taa_resolve_reference(region,previous,8.f,{0.f,0.f},false);
    assert(reset.rejected&&reset.history_fraction==0.f);
    assert(reset.hdr==region[4].color);
    auto stable=taa_resolve_reference(region,previous,8.f,{0.f,0.f},true);
    assert(!stable.rejected && stable.history_fraction>.85f);
    // Neighborhood clamp prevents old bright ghosts on a flat patch.
    assert(std::abs(stable.hdr[0]-2.f)<1.e-5f);
    assert(std::abs(stable.hdr[1]-1.f)<1.e-5f);
    auto disoccluded=taa_resolve_reference(
        region,previous,12.f,{0.f,0.f},true);
    assert(disoccluded.rejected&&disoccluded.hdr==region[4].color);
    auto fast=taa_resolve_reference(
        region,previous,8.f,{120.f,0.f},true);
    assert(!fast.rejected && fast.history_fraction<stable.history_fraction);
    previous.color[1]=std::numeric_limits<float>::quiet_NaN();
    auto corrupt=taa_resolve_reference(region,previous,8.f,{0.f,0.f},true);
    assert(corrupt.rejected && corrupt.hdr==region[4].color);
    previous.color[1]=1.f;
    region[0].color[0]=10.f; // high-contrast neighborhood
    auto contrast=taa_resolve_reference(
        region,previous,8.f,{.2f,.1f},true);
    assert(!contrast.rejected && contrast.hdr[0]>=2.f);
    // Reprojection with nonfinite velocity cannot sample history.
    auto invalid=taa_resolve_reference(
        region,previous,8.f,{std::numeric_limits<float>::infinity(),0.f},
        true);
    assert(invalid.rejected);
    return 0;
}
