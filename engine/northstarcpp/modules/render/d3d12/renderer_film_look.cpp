// NorthStar filmic look contract; no game-specific or third-party SDK code.
#include "renderer_private.hpp"
namespace northstarcpp::render::d3d12::detail {
void RendererState::set_film_look(
        NorthStarRenderPostFxSettings const& requested) {
    if(!northstarcpp::render::valid_postfx(requested))
        throw std::invalid_argument("Invalid film look parameter, ABI or nonfinite value");
    if(northstarcpp::render::active_postfx(requested) && !postfx_enabled())
        throw std::runtime_error(
            "Film Look needs a compositor. Enable FXAA, HDR or "
            "NORTHSTARCPP_ENABLE_POSTFX_LOOK=1 before creating the renderer");
    // The existing GPU frame uses uploaded constants; the next frame sees
    // the new look. No resource allocation or PSO change at runtime.
    film_look=requested;
}
} // namespace northstarcpp::render::d3d12::detail
