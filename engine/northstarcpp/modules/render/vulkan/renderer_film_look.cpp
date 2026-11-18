// NorthStar Vulkan deferred filmic grading; provider-private optional ABI.
// No RAGE/legacy renderer code is linked or copied.
#include "renderer_private.hpp"
#include <stdexcept>
namespace northstarcpp::render::vulkan {
void Renderer::set_film_look(NorthStarRenderPostFxSettings const& requested) {
    if(!northstarcpp::render::valid_postfx(requested))
        throw std::invalid_argument("Invalid film look parameter or ABI");
    if(requested.chromatic_pixels!=0.f && !hdr_enabled_)
        throw std::invalid_argument(
            "Vulkan chromatic aberration requires NORTHSTARCPP_ENABLE_VULKAN_HDR=1");
    if(northstarcpp::render::active_postfx(requested) && !deferred_enabled_)
        throw std::runtime_error(
            "Vulkan Film Look is supported in deferred mode; enable "
            "NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1 when creating renderer");
    film_look_=requested;
}
} // namespace northstarcpp::render::vulkan
