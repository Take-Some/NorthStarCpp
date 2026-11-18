// Synthetic scale workload derived from the real authored scene objects.
// NEVER label this as the full original KaylaNightCity production world.
#pragma once
#include <northstarcpp/scene.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace northstarcpp::tests {
inline Scene tiled_stress(Scene const& source,unsigned side) {
    if(side<2 || side>8 || source.objects.empty() ||
       source.objects.size()*std::size_t(side)*side>max_scene_objects)
        throw std::invalid_argument("invalid stress tiling dimensions");
    Scene out;
    out.name=source.name+" [SYNTHETIC "+std::to_string(side)+"x"+
        std::to_string(side)+"]";
    out.version=3;
    out.layers=source.layers;
    out.streaming=source.streaming;
    Vector3 minimum=source.objects.front().position;
    Vector3 maximum=minimum;
    for(auto const& object:source.objects) {
        auto const& p=object.position;
        minimum.x=std::min(minimum.x,p.x);minimum.z=std::min(minimum.z,p.z);
        maximum.x=std::max(maximum.x,p.x);maximum.z=std::max(maximum.z,p.z);
    }
    float const step_x=std::max(128.f,maximum.x-minimum.x+64.f);
    float const step_z=std::max(128.f,maximum.z-minimum.z+64.f);
    for(unsigned z=0;z<side;++z)for(unsigned x=0;x<side;++x) {
        float const dx=x*step_x,dz=z*step_z;
        SceneCell cell;
        cell.name="SyntheticCell_"+std::to_string(x)+"_"+std::to_string(z);
        cell.minimum={minimum.x+dx-16.f,-10000.f,minimum.z+dz-16.f};
        cell.maximum={maximum.x+dx+16.f,10000.f,maximum.z+dz+16.f};
        out.cells.push_back(cell);
        for(std::size_t i=0;i<source.objects.size();++i) {
            auto object=source.objects[i];
            object.position.x+=dx;object.position.z+=dz;
            object.node.id=out.objects.size()+1;
            object.node.parent=0;
            object.node.cell=cell.name;object.node.room.clear();
            object.node.script.clear();
            object.mass=0.f; // static geometry streaming baseline
            out.objects.push_back(std::move(object));
        }
    }
    return out;
}
}
