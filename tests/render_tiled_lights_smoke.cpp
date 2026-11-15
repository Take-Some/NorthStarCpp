#include "../engine/northstarcpp/modules/render/shared/tiled_lights.hpp"
#include "../engine/northstarcpp/modules/render/shared/render_light_components.hpp"
#include <cassert>
#include <stdexcept>
int main() {
    using namespace northstarcpp::render;
    // Synthetic 90-degree perspective matrix inverse (row-vector).
    std::array<float,16> inv{
       1,0,0,0,
       0,1,0,0,
       0,0,0,1,
       0,0,1,0};
    NorthStarRenderLight lights[2]{};
    lights[0].position_range[2]=5.f;lights[0].position_range[3]=1.f;
    lights[1].position_range[0]=100.f;lights[1].position_range[2]=5.f;
    lights[1].position_range[3]=1.f;
    auto grid=build_tile_lights(128,128,inv,{0,0,0},lights,2);
    assert(grid.columns==4 && grid.rows==4);
    assert(grid.fallback_tiles==0);
    for(unsigned ty=0;ty<4;++ty)
        for(unsigned tx=0;tx<4;++tx) {
            const auto offset=(ty*4+tx)*light_tile_stride;
            const auto n=grid.indices[offset];
            assert(n<=1);
            if(n)assert(grid.indices[offset+1]==0);
        }
    auto empty=build_tile_lights(43,65,inv,{0,0,0},nullptr,0);
    assert(empty.columns==2 && empty.rows==3);
    assert(empty.indices.size()==6*light_tile_stride);
    // Saturation does not silently drop lights: tile evaluates all lights.
    NorthStarRenderLight many[80]{};
    for(auto& light:many) {
        light.position_range[2]=5.f;
        light.position_range[3]=10.f;
    }
    auto overflow=build_tile_lights(32,32,inv,{0,0,0},many,80);
    assert(overflow.fallback_tiles==1);
    assert(overflow.indices[0]==overflow_light_tile);
    bool invalid=false;
    try {(void)build_tile_lights(32,32,inv,{0,0,0},nullptr,2);}
    catch(std::invalid_argument const&) {invalid=true;}
    assert(invalid);
    northstarcpp::Scene scene;
    northstarcpp::SceneObject actor;
    actor.node.id=77;actor.position={2.f,3.f,4.f};
    scene.objects.push_back(actor);
    scene.components.push_back({77,"engine.render.light",1,
        "0 1 0 8 1 0.5 0.25 2 0 -1 0 0.8"});
    auto scene_lights=scene_render_lights(scene);
    assert(scene_lights.size()==1);
    assert(scene_lights[0].position_range[0]==2.f);
    assert(scene_lights[0].position_range[1]==4.f);
    assert(scene_lights[0].position_range[2]==4.f);
    assert(scene_lights[0].direction_cone[3]==0.8f);
    scene.components[0].payload="0 0 0 -1 1 1 1 1 0 0 0 -1";
    invalid=false;
    try{(void)scene_render_lights(scene);}
    catch(std::invalid_argument const&){invalid=true;}
    assert(invalid);
}
