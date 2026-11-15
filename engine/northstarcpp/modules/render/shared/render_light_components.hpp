#pragma once
// Scene V3 component bridge for first-party dynamic lighting.
// Payload is UTF-8 ASCII numbers, version 1:
// "x y z range r g b intensity dx dy dz coneCos"
// The position is relative to the owning entity when entity_id != 0.
// A negative coneCos means point light; 0..1 means spot light.
// Only the native material-based deferred path receives these lights.
#include <northstarcpp/scene.hpp>
#include <northstarcpp/render_lights_api.h>
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace northstarcpp::render {
inline std::vector<NorthStarRenderLight> scene_render_lights(
    northstarcpp::Scene const& scene) {
    std::vector<NorthStarRenderLight> lights;
    for(auto const& component:scene.components) {
        if(component.type!="engine.render.light" || component.version!=1)
            continue;
        if(component.payload.size()>512)
            throw std::invalid_argument("Oversized engine.render.light component");
        if(lights.size()>=NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS)
            throw std::invalid_argument("Too many dynamic scene lights");
        NorthStarRenderLight light{};
        std::istringstream input(component.payload);
        input.imbue(std::locale::classic());
        for(float& v:light.position_range)
            if(!(input>>v))throw std::invalid_argument("Invalid light position/range payload");
        for(float& v:light.color_intensity)
            if(!(input>>v))throw std::invalid_argument("Invalid light color payload");
        for(float& v:light.direction_cone)
            if(!(input>>v))throw std::invalid_argument("Invalid light direction payload");
        std::string extra;
        if(input>>extra)throw std::invalid_argument("Trailing data in dynamic light component");
        for(float v:light.position_range)
            if(!std::isfinite(v))throw std::invalid_argument("Nonfinite light position");
        for(float v:light.color_intensity)
            if(!std::isfinite(v)||v<0)throw std::invalid_argument("Invalid light intensity");
        for(float v:light.direction_cone)
            if(!std::isfinite(v))throw std::invalid_argument("Nonfinite light cone");
        if(light.position_range[3]<=0||light.position_range[3]>100000||
           light.direction_cone[3]<-1||light.direction_cone[3]>1)
            throw std::invalid_argument("Invalid light range/cone");
        if(light.direction_cone[3]>=0) {
            float const len=light.direction_cone[0]*light.direction_cone[0]+
                light.direction_cone[1]*light.direction_cone[1]+
                light.direction_cone[2]*light.direction_cone[2];
            if(len<1.e-8f)throw std::invalid_argument("Zero-length spotlight axis");
        }
        if(component.entity_id) {
            auto const it=std::find_if(scene.objects.begin(),scene.objects.end(),
                [&](auto const& object){return object.node.id==component.entity_id;});
            if(it==scene.objects.end())throw std::invalid_argument("Light has unknown entity");
            if(!it->node.visible)continue;
            light.position_range[0]+=it->position.x;
            light.position_range[1]+=it->position.y;
            light.position_range[2]+=it->position.z;
        }
        lights.push_back(light);
    }
    return lights;
}
}
