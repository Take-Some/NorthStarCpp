#include "../engine/northstarcpp/modules/render/shared/render_light_components.hpp"
#include "../engine/northstarcpp/modules/render/shared/tiled_lights.hpp"
#include "../engine/northstarcpp/modules/editor/editor_preview_studio_lighting.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <string>

int main() {
    using namespace northstarcpp;
    using namespace northstarcpp::render;
    Scene scene;scene.version=3;
    for(auto kind:{LightKind::Point,LightKind::Spot,
                   LightKind::Directional,LightKind::Sky}) {
        auto original=default_scene_light(kind);
        auto encoded=encode_scene_light(original);
        SceneComponentBlob component{0,"engine.render.light",2,encoded};
        auto parsed=decode_scene_light(component);
        assert(parsed.kind==kind);
        assert(parsed.gpu.color_intensity[3]==original.gpu.color_intensity[3]);
        scene.components.push_back(component);
    }
    assert(scene_render_lights(scene).size()==4);
    auto lights=scene_render_lights(scene);
    assert(lights[0].direction_cone[3]==-1.f);
    assert(lights[1].direction_cone[3]>=0.f);
    assert(lights[2].direction_cone[3]==-2.f);
    assert(lights[3].direction_cone[3]==-3.f);
    SceneComponentBlob oldPoint{0,"engine.render.light",1,
        "0 0 0 18 1 0.94 0.78 8 0 -1 0 -1"};
    SceneComponentBlob oldSpot{0,"engine.render.light",1,
        "0 0 0 26 1 0.94 0.78 12 0 -1 0 .82"};
    assert(decode_scene_light(oldPoint).kind==LightKind::Point);
    assert(decode_scene_light(oldSpot).kind==LightKind::Spot);
    scene.components={oldPoint,oldSpot};
    assert(scene_render_lights(scene).size()==2);
    bool invalid=false;
    try {
        auto broken=scene.components[0];broken.version=2;
        broken.payload="directional 0 0 0 1 1 1 1 1 0 0 0 -2";
        (void)decode_scene_light(broken);
    }catch(std::invalid_argument const&){invalid=true;}
    assert(invalid);
    invalid=false;
    try{
        auto broken=default_scene_light(LightKind::Sky);
        broken.gpu.color_intensity[3]=-3;
        (void)encode_scene_light(broken);
    }catch(std::invalid_argument const&){invalid=true;}
    assert(invalid);
    // Spotlight and directional axes inherit actor rotation, not a renderer
    // hardcode; the scene author can rotate a light via the normal gizmo.
    Scene oriented;oriented.version=3;
    SceneObject rotated;rotated.node.id=73;rotated.node.visible=true;
    rotated.node.rotation.z=90.f;
    oriented.objects.push_back(rotated);
    oriented.components.push_back({73,"engine.render.light",2,
        encode_scene_light(default_scene_light(LightKind::Directional))});
    auto directional=scene_render_lights(oriented);
    assert(directional.size()==1);
    assert(std::abs(directional[0].direction_cone[0]-1.f)<1.e-4f);
    assert(std::abs(directional[0].direction_cone[1])<1.e-4f);
    // World-wide lighting must not be incorrectly culled by tile frustums.
    std::array<float,16> inverse{
        1,0,0,0,0,1,0,0,0,0,0,1,0,0,1,0};
    auto globals=scene_render_lights(Scene{});
    (void)globals;
    NorthStarRenderLight global[2]{lights[0],lights[1]};
    global[0]=default_scene_light(LightKind::Directional).gpu;
    global[1]=default_scene_light(LightKind::Sky).gpu;
    auto grid=build_tile_lights(64,64,inverse,{0,0,0},global,2);
    assert(grid.columns==2&&grid.rows==2);
    for(unsigned n=0;n<4;++n) {
        assert(grid.indices[n*light_tile_stride]==2);
        assert(grid.indices[n*light_tile_stride+1]==0);
        assert(grid.indices[n*light_tile_stride+2]==1);
    }
    // Regression for dark prefab previews: lights must be visible even if
    // prefab geometry is translated far away from the world origin.
    Scene preview;preview.version=3;
    preview.components.push_back({0,"engine.render.light",2,
        encode_scene_light(default_scene_light(LightKind::Point))});
    editor::studio_preview::apply_lighting(
        preview,{12000.f,48.f,-3400.f},150.f,1.f);
    auto studio=scene_render_lights(preview);
    assert(studio.size()==4);
    assert(studio[0].direction_cone[3]==-3.f); // Global ambient
    assert(studio[1].direction_cone[3]==-2.f);
    assert(studio[2].direction_cone[3]==-2.f);
    assert(studio[3].direction_cone[3]==-1.f);
    assert(studio[0].color_intensity[3]>.5f);
    assert(studio[3].position_range[0]>11000.f);
    assert(studio[3].position_range[2]<-3400.f);
    assert(studio[3].position_range[3]>=150.f);
    auto const original=studio[1].color_intensity[3];
    editor::studio_preview::apply_lighting(
        preview,{12000.f,48.f,-3400.f},150.f,2.f);
    studio=scene_render_lights(preview);
    assert(studio.size()==4); // No duplicated lights on slider changes
    assert(studio[1].color_intensity[3]>original*1.9f);
    editor::studio_preview::apply_lighting(
        preview,{0.f,0.f,0.f},1.f,1.f);
    studio=scene_render_lights(preview);
    assert(studio.size()==4);
    assert(studio[3].position_range[3]>=12.f); // Small prefabs also lit
    return 0;
}
