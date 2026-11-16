#include "renderer_private.hpp"
#include <cmath>
#include <stdexcept>
#include <unordered_map>
namespace northstarcpp::render::vulkan {
void Renderer::set_pick_triangles(const NorthStarRenderPickVertex* data,
                                  std::uint32_t count) {
    if(count%3 || count>NORTHSTARCPP_RENDER_PICK_MAX_VERTICES || (count&&!data))
        throw std::invalid_argument("Invalid Vulkan object-ID mesh");
    for(std::uint32_t i=0;i<count;i+=3) {
        auto const id=data[i].entity_id;
        for(unsigned j=0;j<3;++j) {
            auto const& v=data[i+j];
            if(v.entity_id!=id || v.reserved || !std::isfinite(v.x) ||
               !std::isfinite(v.y) || !std::isfinite(v.z))
                throw std::invalid_argument("Invalid Vulkan pick triangle");
        }
    }
    if(count)pick_world_vertices_.assign(data,data+count);
    else pick_world_vertices_.clear();
}
void Renderer::set_pick_cubes(const std::uint64_t* ids,std::uint32_t count) {
    if(count>200000u || (count&&!ids))
        throw std::invalid_argument("Invalid Vulkan object IDs");
    if(count)pick_cube_ids_.assign(ids,ids+count);
    else pick_cube_ids_.clear();
}
std::uint64_t Renderer::pick_entity(std::uint32_t x,std::uint32_t y) {
    pick_entity_mode_=true;last_picked_entity_=0;
    try {pick_cube(x,y);}
    catch(...) {pick_entity_mode_=false;throw;}
    pick_entity_mode_=false;
    return last_picked_entity_;
}
std::vector<ColoredVertex> Renderer::build_pick_vertices(
    std::vector<std::uint64_t>& ids) {
    std::vector<ColoredVertex> result;
    result.reserve(frame_vertices_.size()+pick_world_vertices_.size());
    std::unordered_map<std::uint64_t,std::uint32_t> cache;
    auto token=[&](std::uint64_t id)->std::uint32_t {
        if(!id)return 0;
        if(auto found=cache.find(id);found!=cache.end())return found->second;
        if(ids.size()>=0xfffffeu)throw std::runtime_error("Object ID limit exceeded");
        std::uint32_t value=static_cast<std::uint32_t>(ids.size()+1);
        ids.push_back(id);cache.emplace(id,value);return value;
    };
    auto color=[](ColoredVertex& v,std::uint32_t value){
        v.color[0]=float(value&255u)/255.f;
        v.color[1]=float((value>>8)&255u)/255.f;
        v.color[2]=float((value>>16)&255u)/255.f;
        v.color[3]=1.f;
    };
    if(pick_cube_ids_.size()!=pick_cube_ranges_.size())
        throw std::runtime_error("Vulkan cube IDs do not match draw count");
    for(std::size_t i=0;i<pick_cube_ranges_.size();++i) {
        const auto range=pick_cube_ranges_[i];
        if(std::size_t(range.first)+range.count>frame_vertices_.size())
            throw std::runtime_error("Invalid Vulkan pick cube vertices");
        auto const value=token(pick_cube_ids_[i]);
        if(!value)continue;
        for(std::uint32_t j=0;j<range.count;++j) {
            auto v=frame_vertices_[range.first+j];color(v,value);
            result.push_back(v);
        }
    }
    auto const& f=pick_camera_;
    auto const near_clip=f.camera_near_clip>0.f?f.camera_near_clip:0.1f;
    auto const far_clip=f.camera_far_clip>near_clip?f.camera_far_clip:250.f;
    auto const fov=f.camera_fov_y_degrees>1.f?f.camera_fov_y_degrees:70.f;
    auto const scale=1.f/std::tan(fov*3.14159265f/360.f);
    auto const aspect=float(extent_.width)/float(extent_.height);
    auto const cy=std::cos(f.camera_yaw),sy=std::sin(f.camera_yaw);
    auto const cp=std::cos(f.camera_pitch),sp=std::sin(f.camera_pitch);
    auto project=[&](NorthStarRenderPickVertex const& p,ColoredVertex& v) {
        auto dx=p.x-f.camera_x,dy=p.y-f.camera_y,dz=p.z-f.camera_z;
        auto horizontal=dx*cy-dz*sy,forward=dx*sy+dz*cy;
        auto vertical=dy*cp-forward*sp,distance=dy*sp+forward*cp;
        if(distance<=near_clip)return false;
        v.clip[0]=horizontal*scale/aspect;
        v.clip[1]=-vertical*scale;
        v.clip[2]=(far_clip*distance-far_clip*near_clip)/(far_clip-near_clip);
        v.clip[3]=distance;return true;
    };
    for(std::size_t i=0;i<pick_world_vertices_.size();i+=3) {
        auto const id=pick_world_vertices_[i].entity_id;
        if(!id)continue;
        ColoredVertex tri[3]{};
        bool valid=true;
        for(unsigned j=0;j<3;++j)
            valid=project(pick_world_vertices_[i+j],tri[j])&&valid;
        if(!valid)continue;
        auto const value=token(id);
        for(auto& v:tri){color(v,value);result.push_back(v);}
    }
    if(result.size()*sizeof(ColoredVertex)>max_vertex_bytes_)
        throw std::runtime_error("Vulkan pick buffer capacity exceeded");
    return result;
}

}
