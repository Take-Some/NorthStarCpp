#pragma once
// Portable validation and neutral defaults for a provider-owned filmic stack.
// Effects operate on scene color BEFORE HUD and are opt-in.
#include <northstarcpp/render_postfx_api.h>
#include <cmath>
namespace northstarcpp::render {
inline constexpr NorthStarRenderPostFxSettings neutral_postfx() noexcept {
    return {sizeof(NorthStarRenderPostFxSettings),0.f,1.f,1.f,0.f,0.f,0.f,0u};
}
inline bool valid_postfx(NorthStarRenderPostFxSettings const& s) noexcept {
    return s.struct_size==sizeof(NorthStarRenderPostFxSettings) &&
           s.reserved==0 &&
           std::isfinite(s.exposure_ev) && s.exposure_ev>=-5.f && s.exposure_ev<=5.f &&
           std::isfinite(s.contrast) && s.contrast>=0.f && s.contrast<=2.f &&
           std::isfinite(s.saturation) && s.saturation>=0.f && s.saturation<=2.f &&
           std::isfinite(s.vignette) && s.vignette>=0.f && s.vignette<=1.f &&
           std::isfinite(s.chromatic_pixels) &&
           s.chromatic_pixels>=0.f && s.chromatic_pixels<=5.f &&
           std::isfinite(s.film_grain) && s.film_grain>=0.f && s.film_grain<=0.2f;
}
inline bool active_postfx(NorthStarRenderPostFxSettings const& s) noexcept {
    return s.exposure_ev!=0.f || s.contrast!=1.f || s.saturation!=1.f ||
           s.vignette>0.f || s.chromatic_pixels>0.f || s.film_grain>0.f;
}
} // namespace northstarcpp::render
