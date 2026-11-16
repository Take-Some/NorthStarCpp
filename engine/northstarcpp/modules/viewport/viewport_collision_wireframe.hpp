// GPU-facing editor-only collider outlines. No provider API changes.
#pragma once
#include <northstarcpp/collision_wireframe.hpp>
#include <northstarcpp/render_plugin.h>
#include <cmath>
#include <algorithm>
#include <vector>
namespace northstarcpp::viewport_debug {
inline void append_colliders(std::vector<NorthStarRenderCube>& cubes,
                             Scene const& scene,int selected) {
    std::size_t shown=0;
    auto draw=[&](std::size_t index) {
        if(index>=scene.objects.size()||shown>=80)return;
        auto const& actor=scene.objects[index];
        if(!actor.node.visible)return;
        ++shown;
        auto const color=!actor.node.collision?0xda6d70u:
            int(index)==selected?0xffce62u:0x58e1b5u;
        collision_wireframe::draw(scene,actor,[&](Vector3 a,Vector3 b) {
            if(cubes.size()>=max_scene_objects)return;
            Vector3 d{b.x-a.x,b.y-a.y,b.z-a.z};
            float const len=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
            if(len<1.e-6f)return;
            Vector3 t{d.x/len,d.y/len,d.z/len};
            Vector3 seed=std::abs(t.y)>.9f?Vector3{0,0,1}:Vector3{0,1,0};
            Vector3 side{t.y*seed.z-t.z*seed.y,
                         t.z*seed.x-t.x*seed.z,
                         t.x*seed.y-t.y*seed.x};
            float const n=std::sqrt(side.x*side.x+side.y*side.y+side.z*side.z);
            if(n<1.e-6f)return;
            side={side.x/n,side.y/n,side.z/n};
            Vector3 up{t.y*side.z-t.z*side.y,
                       t.z*side.x-t.x*side.z,
                       t.x*side.y-t.y*side.x};
            float const thickness=std::clamp(len*.004f,.01f,.045f);
            NorthStarRenderCube edge{};
            edge.size=1.f;edge.rgb=color;
            edge.x=(a.x+b.x)*.5f;
            edge.y=(a.y+b.y)*.5f-.5f;
            edge.z=(a.z+b.z)*.5f;
            edge.rotation[0]=t.x*len;edge.rotation[3]=t.y*len;
            edge.rotation[6]=t.z*len;
            edge.rotation[1]=side.x*thickness;edge.rotation[4]=side.y*thickness;
            edge.rotation[7]=side.z*thickness;
            edge.rotation[2]=up.x*thickness;edge.rotation[5]=up.y*thickness;
            edge.rotation[8]=up.z*thickness;
            cubes.push_back(edge);
        },int(index)==selected);
    };
    if(selected>=0)draw(static_cast<std::size_t>(selected));
    for(std::size_t index=0;index<scene.objects.size()&&shown<80
        &&cubes.size()<max_scene_objects;++index)
        if(int(index)!=selected)draw(index);
}
} // namespace northstarcpp::viewport_debug
