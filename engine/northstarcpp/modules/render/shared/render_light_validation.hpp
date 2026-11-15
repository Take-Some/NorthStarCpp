// Shared validation for engine.render dynamic point/spot light ABI.
// Hardware render providers must copy, never borrow, host-owned arrays.
#pragma once
#include <northstarcpp/render_lights_api.h>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace northstarcpp::render {
inline std::vector<NorthStarRenderLight> validated_render_lights(
    const NorthStarRenderLight* lights,std::uint32_t count) {
    if(count>NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS || (count&&!lights))
        throw std::invalid_argument("Invalid dynamic light count/pointer");
    std::vector<NorthStarRenderLight> next;
    next.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        auto const& light=lights[i];
        for(float value:light.position_range)
            if(!std::isfinite(value))
                throw std::invalid_argument("Nonfinite light position/range");
        for(float value:light.color_intensity)
            if(!std::isfinite(value))
                throw std::invalid_argument("Nonfinite light color/intensity");
        for(float value:light.direction_cone)
            if(!std::isfinite(value))
                throw std::invalid_argument("Nonfinite light direction/cone");
        if(light.position_range[3]<=0.f || light.color_intensity[3]<0.f ||
           light.color_intensity[0]<0.f ||light.color_intensity[1]<0.f ||
           light.color_intensity[2]<0.f ||
           light.direction_cone[3]<-1.f || light.direction_cone[3]>1.f)
            throw std::invalid_argument("Invalid dynamic light photometry");
        if(light.direction_cone[3]>=0.f) {
            float squared=light.direction_cone[0]*light.direction_cone[0]+
                light.direction_cone[1]*light.direction_cone[1]+
                light.direction_cone[2]*light.direction_cone[2];
            if(squared<.001f)
                throw std::invalid_argument("Spotlight direction must be nonzero");
        }
        next.push_back(light);
    }
    return next;
}
}
