// Collision geometry baked from an OBJ/YDD/YDR drawable into a scene component.
// Fixed-endian, bounded, provider-neutral; no editor or renderer dependencies.
#pragma once
#include <northstarcpp/collision_authoring.hpp>
#include <northstarcpp/physics_surface.hpp>
#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/assets.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace northstarcpp::collision_mesh {
inline constexpr char component_type[]="northstar.physics.collider.mesh";
inline constexpr std::uint32_t version=3;
inline constexpr std::size_t max_bytes=900000, max_verts=18000, max_indices=54000;
struct Mesh {
    std::string source;
    std::vector<float> xyz;
    std::vector<std::uint32_t> indices;
    // One stable Physics Material ID per source triangle, if available.
    // Empty for legacy NCM1 and single-material fallback colliders.
    std::vector<std::uint32_t> triangle_materials;
    // Raw source-format IDs are preserved separately from normalized surfaces.
    std::vector<std::uint32_t> source_material_ids;
};
inline bool valid(Mesh const& m) {
    if(m.source.empty()||m.source.size()>256||m.xyz.empty()||
       m.xyz.size()%3||m.xyz.size()/3>max_verts||
       m.indices.empty()||m.indices.size()%3||m.indices.size()>max_indices||
       20+m.source.size()+m.xyz.size()*4+m.indices.size()*4+
           (m.triangle_materials.size()+m.source_material_ids.size())*4>max_bytes ||
       (!m.triangle_materials.empty() &&
        m.triangle_materials.size()!=m.indices.size()/3) ||
       (!m.source_material_ids.empty() &&
        (m.source_material_ids.size()!=m.indices.size()/3 ||
         m.triangle_materials.empty())))
        return false;
    for(float v:m.xyz)if(!std::isfinite(v)||std::abs(v)>100000.f)return false;
    for(auto i:m.indices)if(i>=m.xyz.size()/3)return false;
    for(auto id:m.triangle_materials)if(id>physics_surface::maximum)return false;
    return true;
}
inline void append32(std::string& data,std::uint32_t v) {
    for(int n=0;n<4;++n)data.push_back(char((v>>(n*8))&255u));
}
inline std::uint32_t read32(std::string const& s,std::size_t& at) {
    if(s.size()-at<4)throw std::invalid_argument("Truncated collision mesh");
    std::uint32_t v=0;
    for(int n=0;n<4;++n)v|=std::uint32_t(std::uint8_t(s[at++]))<<(n*8);
    return v;
}
inline std::string encode(Mesh const& m) {
    if(!valid(m))throw std::invalid_argument("Collision mesh exceeds limits or is invalid");
    std::string data=!m.source_material_ids.empty()?"NCM3":
        m.triangle_materials.empty()?"NCM1":"NCM2";
    append32(data,std::uint32_t(m.source.size()));
    append32(data,std::uint32_t(m.xyz.size()/3));
    append32(data,std::uint32_t(m.indices.size()));
    data+=m.source;
    for(float value:m.xyz) {
        std::uint32_t bits=0;
        std::memcpy(&bits,&value,sizeof(bits));
        append32(data,bits);
    }
    for(auto index:m.indices)append32(data,index);
    for(auto id:m.triangle_materials)append32(data,id);
    for(auto id:m.source_material_ids)append32(data,id);
    return data;
}
inline std::optional<Mesh> decode(std::string const& data) {
    try {
        if(data.size()>max_bytes||data.size()<16||
           (data.substr(0,4)!="NCM1" && data.substr(0,4)!="NCM2" &&
            data.substr(0,4)!="NCM3"))
            return std::nullopt;
        std::size_t at=4;
        auto source_len=read32(data,at);
        auto verts=read32(data,at);
        auto count=read32(data,at);
        if(source_len==0||source_len>256||verts==0||verts>max_verts||
           count==0||count>max_indices||count%3||
           16ull+source_len+12ull*verts+4ull*count+
               (data.substr(0,4)!="NCM1"?4ull*(count/3):0ull)+
               (data.substr(0,4)=="NCM3"?4ull*(count/3):0ull)!=data.size())
            return std::nullopt;
        Mesh m;
        m.source=data.substr(at,source_len);at+=source_len;
        m.xyz.reserve(std::size_t(verts)*3);m.indices.reserve(count);
        for(std::uint32_t i=0;i<verts*3;++i)
            {
                auto const bits=read32(data,at);
                float value=0.f;
                std::memcpy(&value,&bits,sizeof(value));
                m.xyz.push_back(value);
            }
        for(std::uint32_t i=0;i<count;++i)m.indices.push_back(read32(data,at));
        if(data.substr(0,4)!="NCM1")
            for(std::uint32_t i=0;i<count/3;++i)
                m.triangle_materials.push_back(read32(data,at));
        if(data.substr(0,4)=="NCM3")
            for(std::uint32_t i=0;i<count/3;++i)
                m.source_material_ids.push_back(read32(data,at));
        return valid(m)?std::optional<Mesh>(std::move(m)):std::nullopt;
    }catch(...){return std::nullopt;}
}
inline std::optional<Mesh> lookup(Scene const& scene,std::uint64_t entity) {
    for(auto const& c:scene.components)
        if(c.entity_id==entity && c.type==component_type &&
           (c.version==1 || c.version==2 || c.version==3))
            return decode(c.payload);
    return std::nullopt;
}
inline void assign(Scene& scene,std::uint64_t entity,Mesh const& mesh) {
    if(!entity)throw std::invalid_argument("Invalid collision mesh owner");
    SceneComponentBlob c{entity,component_type,
        !mesh.source_material_ids.empty()?3u:
        mesh.triangle_materials.empty()?1u:2u,encode(mesh)};
    for(auto& current:scene.components)
        if(current.entity_id==entity&&current.type==component_type){
            current=std::move(c);return;
        }
    scene.components.push_back(std::move(c));
}
inline Mesh bake(AssetStore const& assets,std::string const& reference) {
    auto asset=load_scene_mesh(assets,reference);
    Mesh m;m.source=reference;
    m.xyz.reserve(asset.vertices.size()*3);
    for(auto const& v:asset.vertices) {
        m.xyz.push_back(v.position.x);
        m.xyz.push_back(v.position.y);
        m.xyz.push_back(v.position.z);
    }
    m.indices=std::move(asset.indices);
    if(!asset.sections.empty()) {
        m.triangle_materials.assign(m.indices.size()/3,0);
        for(auto const& section:asset.sections) {
            if(section.first_index>m.indices.size() ||
               section.index_count>m.indices.size()-section.first_index ||
               section.first_index%3 || section.index_count%3)
                throw std::invalid_argument("Material section has invalid triangle range");
            auto const material=physics_surface::from_material_reference(
                section.material_reference.empty()?section.name:
                section.material_reference);
            for(std::size_t i=section.first_index/3;
                i<(section.first_index+section.index_count)/3;++i)
                m.triangle_materials[i]=material;
        }
    }
    if(!valid(m))throw std::length_error(
        "Complex collision exceeds 18k vertices / 18k triangles / 900 KiB; use a lower-poly mesh");
    return m;
}
// Scene transform is applied at physics load, not baked into the asset.
// This keeps authored rotation, scale and size editable after cooking.
inline std::vector<float> transform(Mesh const& m,SceneObject const& o,
                                   collision_authoring::Collider const& config,
                                   bool relative_to_body_center) {
    float const rad=3.141592653589793f/180.f;
    float const ax=o.node.rotation.x*rad,ay=o.node.rotation.y*rad,az=o.node.rotation.z*rad;
    float const sx=std::sin(ax),cx=std::cos(ax),sy=std::sin(ay),cy=std::cos(ay),
                sz=std::sin(az),cz=std::cos(az);
    float const matrix[9]{cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx,
                          sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx,
                          -sy,cy*sx,cy*cx};
    std::vector<float> xyz;xyz.reserve(m.xyz.size());
    for(std::size_t i=0;i<m.xyz.size();i+=3){
        float const x=m.xyz[i]*o.size*o.node.scale.x;
        float const y=m.xyz[i+1]*o.size*o.node.scale.y;
        float const z=m.xyz[i+2]*o.size*o.node.scale.z;
        xyz.push_back(matrix[0]*x+matrix[1]*y+matrix[2]*z+config.offset.x);
        xyz.push_back(matrix[3]*x+matrix[4]*y+matrix[5]*z+config.offset.y-
                      (relative_to_body_center?o.size*.5f:0.f));
        xyz.push_back(matrix[6]*x+matrix[7]*y+matrix[8]*z+config.offset.z);
    }
    return xyz;
}
} // namespace northstarcpp::collision_mesh
