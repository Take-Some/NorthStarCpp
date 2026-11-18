#pragma once
// Typed scene light authoring. v1 (legacy) remains readable:
// x y z range r g b intensity dx dy dz cosine
// v2 stores an explicit kind followed by the same 12 numeric parameters:
// point|spot|directional|sky x y z range r g b intensity dx dy dz coneCos
// GPU ABI v1 is untouched: coneCos -1 point, -2 directional, -3 sky;
// [0,1] spot. Global lights use a radius-free tiled evaluation.
#include <northstarcpp/scene.hpp>
#include <northstarcpp/render_lights_api.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>

namespace northstarcpp::render {
enum class LightKind { Point, Spot, Directional, Sky };
inline constexpr const char* light_kind_name(LightKind kind) {
    switch(kind) {
    case LightKind::Point:return "point";
    case LightKind::Spot:return "spot";
    case LightKind::Directional:return "directional";
    case LightKind::Sky:return "sky";
    }
    return "point";
}
inline LightKind light_kind_from_name(std::string const& name) {
    if(name=="point")return LightKind::Point;
    if(name=="spot")return LightKind::Spot;
    if(name=="directional")return LightKind::Directional;
    if(name=="sky")return LightKind::Sky;
    throw std::invalid_argument("Unknown scene light type: "+name);
}
struct SceneLight {
    LightKind kind=LightKind::Point;
    NorthStarRenderLight gpu{};
};
inline SceneLight default_scene_light(LightKind kind) {
    SceneLight light;
    light.kind=kind;
    auto& g=light.gpu;
    g.position_range[3]=(kind==LightKind::Spot?26.f:
                         kind==LightKind::Point?18.f:1.f);
    g.color_intensity[0]=1.f;
    g.color_intensity[1]=.94f;
    g.color_intensity[2]=.78f;
    g.color_intensity[3]=kind==LightKind::Spot?12.f:
                         kind==LightKind::Point?8.f:
                         kind==LightKind::Directional?2.f:.30f;
    g.direction_cone[1]=-1.f;
    g.direction_cone[3]=kind==LightKind::Spot?.82f:
                        kind==LightKind::Directional?-2.f:
                        kind==LightKind::Sky?-3.f:-1.f;
    return light;
}
inline void validate_scene_light(SceneLight const& light) {
    auto const& g=light.gpu;
    for(float x:g.position_range)
        if(!std::isfinite(x))throw std::invalid_argument("Nonfinite light position");
    for(float x:g.color_intensity)
        if(!std::isfinite(x)||x<0.f||x>100000.f)
            throw std::invalid_argument("Invalid light color/intensity");
    for(float x:g.direction_cone)
        if(!std::isfinite(x))throw std::invalid_argument("Nonfinite light direction");
    if(!(g.position_range[3]>0.f) || g.position_range[3]>100000.f)
        throw std::invalid_argument("Invalid light range");
    auto const tag=g.direction_cone[3];
    if((light.kind==LightKind::Point && tag!=-1.f) ||
       (light.kind==LightKind::Directional && tag!=-2.f) ||
       (light.kind==LightKind::Sky && tag!=-3.f) ||
       (light.kind==LightKind::Spot && (tag<0.f||tag>=1.f)))
        throw std::invalid_argument("Light type/cone mismatch");
    if(light.kind==LightKind::Spot||light.kind==LightKind::Directional) {
        float const dx=g.direction_cone[0],dy=g.direction_cone[1],
                    dz=g.direction_cone[2];
        float const len2=dx*dx+dy*dy+dz*dz;
        if(len2<1.e-8f||!std::isfinite(len2))
            throw std::invalid_argument("Zero or invalid light direction");
    }
}
inline SceneLight decode_scene_light(northstarcpp::SceneComponentBlob const& component) {
    if(component.type!="engine.render.light" ||
       (component.version!=1 && component.version!=2))
        throw std::invalid_argument("Unsupported light component");
    if(component.payload.size()>512)
        throw std::invalid_argument("Oversized engine.render.light component");
    std::istringstream input(component.payload);
    input.imbue(std::locale::classic());
    SceneLight light;
    if(component.version==2) {
        std::string kind;
        if(!(input>>kind))throw std::invalid_argument("Missing light type");
        light.kind=light_kind_from_name(kind);
    }
    for(float& v:light.gpu.position_range)
        if(!(input>>v))throw std::invalid_argument("Invalid light position/range");
    for(float& v:light.gpu.color_intensity)
        if(!(input>>v))throw std::invalid_argument("Invalid light color/intensity");
    for(float& v:light.gpu.direction_cone)
        if(!(input>>v))throw std::invalid_argument("Invalid light direction/cone");
    std::string extra;
    if(input>>extra)throw std::invalid_argument("Unexpected extra scene light data");
    if(component.version==1) {
        if(light.gpu.direction_cone[3]>=0.f)
            light.kind=LightKind::Spot;
        else {
            if(light.gpu.direction_cone[3]<-1.f)
                throw std::invalid_argument("Invalid legacy point light sentinel");
            light.kind=LightKind::Point;
            light.gpu.direction_cone[3]=-1.f; // Accept legacy [-1,0) variants.
        }
    }
    validate_scene_light(light);
    return light;
}
inline std::string encode_scene_light(SceneLight const& light) {
    validate_scene_light(light);
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output<<std::setprecision(9)<<light_kind_name(light.kind);
    for(float v:light.gpu.position_range)output<<' '<<v;
    for(float v:light.gpu.color_intensity)output<<' '<<v;
    for(float v:light.gpu.direction_cone)output<<' '<<v;
    return output.str();
}
inline std::vector<NorthStarRenderLight> scene_render_lights(
    northstarcpp::Scene const& scene) {
    std::vector<NorthStarRenderLight> lights;
    // O(objects+lights) lookup; avoid scanning the scene for every light.
    std::unordered_map<std::uint64_t,SceneObject const*> entities;
    entities.reserve(scene.objects.size());
    for(auto const& object:scene.objects)
        entities.emplace(object.node.id,&object);
    for(auto const& component:scene.components) {
        if(component.type!="engine.render.light")continue;
        if(lights.size()>=NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS)
            throw std::invalid_argument("Too many scene lights");
        auto source=decode_scene_light(component);
        auto light=source.gpu;
        if(component.entity_id) {
            auto it=entities.find(component.entity_id);
            if(it==entities.end())
                throw std::invalid_argument("Light has unknown entity");
            if(!it->second->node.visible)continue;
            light.position_range[0]+=it->second->position.x;
            light.position_range[1]+=it->second->position.y;
            light.position_range[2]+=it->second->position.z;
            if(source.kind==LightKind::Spot ||
               source.kind==LightKind::Directional) {
                // Match the editor's authored Rz * Ry * Rx Euler basis.
                constexpr float rad=.01745329251994329577f;
                float const x=it->second->node.rotation.x*rad;
                float const y=it->second->node.rotation.y*rad;
                float const z=it->second->node.rotation.z*rad;
                float const sx=std::sin(x),cx=std::cos(x);
                float const sy=std::sin(y),cy=std::cos(y);
                float const sz=std::sin(z),cz=std::cos(z);
                float const basis[9]{
                    cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx,
                    sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx,
                    -sy,cy*sx,cy*cx};
                float const dx=light.direction_cone[0],
                            dy=light.direction_cone[1],
                            dz=light.direction_cone[2];
                for(int row=0;row<3;++row)
                    light.direction_cone[row]=basis[row*3]*dx+
                        basis[row*3+1]*dy+basis[row*3+2]*dz;
                float const norm=std::sqrt(
                    light.direction_cone[0]*light.direction_cone[0]+
                    light.direction_cone[1]*light.direction_cone[1]+
                    light.direction_cone[2]*light.direction_cone[2]);
                if(norm>1.e-8f)
                    for(int j=0;j<3;++j)light.direction_cone[j]/=norm;
            }
        }
        lights.push_back(light);
    }
    return lights;
}
}
