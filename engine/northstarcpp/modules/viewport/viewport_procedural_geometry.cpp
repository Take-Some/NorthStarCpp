#include "viewport_procedural_geometry.hpp"
#include <cmath>
#include <stdexcept>

namespace northstarcpp::viewport_detail {
void append_procedural_geometry(
    std::vector<NorthStarMaterialVertex>& vertices,
    GeometryData const& data,
    SceneObject const& object,
    NorthStarRenderCube const& cube,
    std::vector<NorthStarRenderPickVertex>& pick_vertices,
    bool collect_pick,std::uint64_t entity_id,bool& pick_complete) {
    if(data.indices.size()%3)
        throw std::invalid_argument("Invalid procedural triangle topology");
    auto transformed=[&](Vector3 v) {
        return Vector3{
            object.position.x+cube.rotation[0]*v.x+cube.rotation[1]*v.y+cube.rotation[2]*v.z,
            object.position.y+cube.rotation[3]*v.x+cube.rotation[4]*v.y+cube.rotation[5]*v.z,
            object.position.z+cube.rotation[6]*v.x+cube.rotation[7]*v.y+cube.rotation[8]*v.z};
    };
    for(std::size_t k=0;k<data.indices.size();k+=3) {
        Vector3 local[3],world[3];
        for(unsigned v=0;v<3;++v) {
            auto const off=std::size_t(data.indices[k+v])*3;
            if(off+2>=data.xyz.size())
                throw std::invalid_argument("Invalid procedural mesh index");
            local[v]={data.xyz[off],data.xyz[off+1],data.xyz[off+2]};
            world[v]=transformed({local[v].x*object.size,
                local[v].y*object.size,local[v].z*object.size});
        }
        auto const u=Vector3{world[1].x-world[0].x,
            world[1].y-world[0].y,world[1].z-world[0].z};
        auto const v=Vector3{world[2].x-world[0].x,
            world[2].y-world[0].y,world[2].z-world[0].z};
        auto const cross=Vector3{u.y*v.z-u.z*v.y,
            u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        auto const len=std::sqrt(cross.x*cross.x+cross.y*cross.y+cross.z*cross.z);
        auto const n=len>1.e-6f?Vector3{cross.x/len,cross.y/len,cross.z/len}:
            Vector3{0.f,1.f,0.f};
        for(unsigned idx=0;idx<3;++idx) {
            auto const& p=local[idx];
            float tu,tv;
            if(std::abs(n.y)>=std::max(std::abs(n.x),std::abs(n.z)))
                {tu=p.x;tv=p.z;}
            else if(std::abs(n.x)>std::abs(n.z))
                {tu=p.z;tv=p.y;}
            else {tu=p.x;tv=p.y;}
            if(vertices.size()>=NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES)
                throw std::runtime_error("Material mesh vertex limit exceeded");
            vertices.push_back({world[idx].x,world[idx].y,world[idx].z,
                n.x,n.y,n.z,tu,tv});
            if(collect_pick && entity_id && pick_complete) {
                if(pick_vertices.size()>=180000u)pick_complete=false;
                else pick_vertices.push_back(
                    {world[idx].x,world[idx].y,world[idx].z,0,entity_id});
            }
        }
    }
}
} // namespace northstarcpp::viewport_detail
