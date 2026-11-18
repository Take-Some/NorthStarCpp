#pragma once
// Isolated asset/prefab studio lighting. No changes to authored Scene V3
// or production environment settings. The GPU light types are shared by
// Vulkan and D3D12; global lights are not culled by local radius/tile size.
#include "../render/shared/render_light_components.hpp"
#include <northstarcpp/scene.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace northstarcpp::editor::studio_preview {
inline void apply_lighting(Scene& scene,
                           std::array<float,3> const& center,
                           float visible_extent,
                           float brightness) {
    if(!std::isfinite(visible_extent) || visible_extent<=0.f ||
       !std::isfinite(brightness))
        throw std::invalid_argument("Invalid prefab studio illumination");
    for(float coordinate:center)
        if(!std::isfinite(coordinate))
            throw std::invalid_argument("Nonfinite prefab bounds center");
    brightness=std::clamp(brightness,0.f,2.5f);

    // Discard lighting of the original scene COPY only. An asset preview
    // must be reproducible regardless of any light entities inside a prefab.
    auto& components=scene.components;
    components.erase(std::remove_if(components.begin(),components.end(),
        [](SceneComponentBlob const& c){
            return c.type=="engine.render.light";
        }),components.end());

    auto add=[&](render::LightKind kind,
                 std::array<float,3> const& rgb,float strength,
                 std::array<float,3> const& position,
                 std::array<float,3> const& direction,
                 float radius) {
        auto fixture=render::default_scene_light(kind);
        for(int axis=0;axis<3;++axis) {
            fixture.gpu.position_range[axis]=position[axis];
            fixture.gpu.color_intensity[axis]=rgb[axis];
            fixture.gpu.direction_cone[axis]=direction[axis];
        }
        fixture.gpu.position_range[3]=radius;
        fixture.gpu.color_intensity[3]=strength;
        SceneComponentBlob component;
        component.entity_id=0;  // Independent of prefab actor hierarchy.
        component.type="engine.render.light";
        component.version=2;
        component.payload=render::encode_scene_light(fixture);
        components.push_back(std::move(component));
    };

    // Broad, soft skylight makes shadowed PBR surfaces readable with both
    // Vulkan and D3D12, independent of their distance from world origin.
    add(render::LightKind::Sky,{.86f,.91f,1.f},.64f*brightness,
        center,{0.f,-1.f,0.f},1.f);
    // Two opposing global directions retain relief while avoiding the
    // previous pitch-black back and side silhouettes.
    add(render::LightKind::Directional,{1.f,.94f,.85f},3.1f*brightness,
        center,{-0.42f,-0.73f,-0.54f},1.f);
    add(render::LightKind::Directional,{.73f,.83f,1.f},1.65f*brightness,
        center,{.72f,-.35f,.57f},1.f);

    // Back separation light follows the actual compiled drawable bounds.
    // Prefabs can be translated or contain very large world-space meshes.
    float const span=std::clamp(visible_extent,1.f,800.f);
    float const radius=std::max(12.f,span*5.f);
    add(render::LightKind::Point,{1.f,.92f,.86f},5.f*brightness,
        {center[0]-span*.8f,center[1]+span*.9f,
         center[2]-span*1.1f},
        {0.f,-1.f,0.f},radius);
}
} // namespace northstarcpp::editor::studio_preview
