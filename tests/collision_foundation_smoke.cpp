// Native foundation collision regression: ABI, hit ordering and editor picking.
#include <northstarcpp/collision_service.hpp>
#include <northstarcpp/geometry_service.hpp>
#include <northstarcpp/geometry_scene.hpp>
#include <northstarcpp/scene.hpp>
#include "editor_camera.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace northstarcpp;
static void require(bool ok,char const* reason) {
    if(!ok) throw std::runtime_error(reason);
}
int main() {
    try {
        CollisionService collision;
        GeometryService geometry;
        require(!collision.provider_name().empty(),"Missing engine.collision provider");
        auto box=collision.ray_box({0,0,-10},{0,0,1},{-1,-1,-1},{1,1,1},100);
        require(box&&std::abs(*box-9.f)<.0001f,"Original slab test AABB distance");
        require(!collision.ray_box({5,0,-10},{0,0,1},
            {-1,-1,-1},{1,1,1},100),"AABB false-positive");
        std::vector<float> vertices={
            -1,-1,10,1,-1,10,0,1,10,
            -1,-1,5,1,-1,5,0,1,5
        };
        std::vector<uint16_t> indices={0,1,2,3,4,5};
        auto nearest=collision.ray_mesh({0,0,0},{0,0,1},vertices,indices,100);
        require(nearest&&std::abs(nearest->distance-5.f)<.0001f&&
            nearest->triangle==1,"Wrong nearest triangle or ordering");
        require(!collision.ray_mesh({20,0,0},{0,0,1},vertices,indices,100),
            "Triangle intersection false positive");
        require(!collision.ray_mesh({0,0,0},{0,0,1},vertices,indices,4.f),
            "Ray range ignored");
        Scene scene{};
        scene.version=3;
        SceneObject obj{};
        obj.position={0,0,6};
        obj.size=2.f;
        obj.node.id=100;
        scene.objects.push_back(obj);
        NorthStarGeometryRequest cube{};
        cube.size=sizeof(cube);
        cube.kind=NORTHSTAR_GEOMETRY_SOLID;
        cube.shape=0;
        scene.components.push_back(make_geometry_primitive_component(100,cube));
        auto pick=editor_tools::pick_actor_geometry(scene,
            {{0,1,0},{0,0,1}},collision,geometry);
        require(pick&&*pick==0,"Procedural cube not selectable by triangle raycast");
        scene.objects[0].node.visible=false;
        require(!editor_tools::pick_actor_geometry(scene,
            {{0,1,0},{0,0,1}},collision,geometry),"Hidden primitive selected");
        std::puts("Original foundation collision and Qt procedural selection passed");
        return 0;
    } catch(std::exception const& e) {
        std::fprintf(stderr,"Foundation collision QA: %s\n",e.what());
        return 1;
    }
}
