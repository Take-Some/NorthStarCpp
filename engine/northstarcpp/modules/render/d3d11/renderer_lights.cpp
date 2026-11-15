// NorthStarCpp D3D11: implementation of the shared dynamic-light ABI.
#include "renderer_private.hpp"
#include "../shared/render_light_validation.hpp"
#include <array>
#include <cstring>

namespace northstarcpp::render::d3d11::detail {
void RendererState::set_dynamic_lights(const NorthStarRenderLight* lights,
                                       std::uint32_t count) {
    dynamic_lights=northstarcpp::render::validated_render_lights(lights,count);
    dynamic_lights_dirty=true;
}
void RendererState::upload_dynamic_lights() {
    if(!dynamic_lights_dirty)return;
    if(!dynamic_light_buffer.get())
        throw std::runtime_error("D3D11 dynamic-light constant buffer is unavailable");
    constexpr auto bytes=16u+NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS*
        sizeof(NorthStarRenderLight);
    std::array<std::uint8_t,bytes> upload{};
    float const count=static_cast<float>(dynamic_lights.size());
    std::memcpy(upload.data(),&count,sizeof(count));
    if(!dynamic_lights.empty()) {
        std::memcpy(upload.data()+16,dynamic_lights.data(),
            dynamic_lights.size()*sizeof(NorthStarRenderLight));
    }
    context->UpdateSubresource(dynamic_light_buffer.get(),0,nullptr,
                               upload.data(),0,0);
    dynamic_lights_dirty=false;
}
} // namespace northstarcpp::render::d3d11::detail
