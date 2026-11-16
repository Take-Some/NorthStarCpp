// Provider-neutral, persisted V-HACD convex collision results.
// Authored in the editor; runtime consumes cooked hulls without voxelization.
#pragma once
#include <northstarcpp/collision_mesh.hpp>
#include <northstarcpp/collision_cooker.hpp>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace northstarcpp::collision_hulls {
inline constexpr char component_type[]="northstar.physics.collider.hulls";
inline constexpr std::uint32_t version=1;
inline constexpr std::size_t max_bytes=900000;
struct Set {
    std::string source;
    std::uint32_t splits_num=0;
    std::vector<collision_cooker::Hull> hulls;
};
inline bool valid(Set const& set) {
    if(set.source.empty()||set.source.size()>256||
       set.splits_num<1||set.splits_num>16 ||
       set.hulls.empty()||set.hulls.size()>set.splits_num)return false;
    std::size_t n=20+set.source.size();
    for(auto const& h:set.hulls) {
        if(h.xyz.size()<12||h.xyz.size()%3||h.xyz.size()>384||
           h.indices.size()<12||h.indices.size()%3||
           h.indices.size()>2048)return false;
        n+=8+4*h.xyz.size()+4*h.indices.size();
        if(n>max_bytes)return false;
        for(float f:h.xyz)
            if(!std::isfinite(f)||std::abs(f)>100000.f)return false;
        for(auto i:h.indices)
            if(i>=h.xyz.size()/3)return false;
    }
    return true;
}
inline std::string encode(Set const& set) {
    if(!valid(set))throw std::invalid_argument("Invalid cooked convex hulls");
    std::string bytes="NCH1";
    collision_mesh::append32(bytes,std::uint32_t(set.source.size()));
    collision_mesh::append32(bytes,set.splits_num);
    collision_mesh::append32(bytes,std::uint32_t(set.hulls.size()));
    bytes+=set.source;
    for(auto const& h:set.hulls) {
        collision_mesh::append32(bytes,std::uint32_t(h.xyz.size()/3));
        collision_mesh::append32(bytes,std::uint32_t(h.indices.size()));
        for(float f:h.xyz) {
            std::uint32_t bits;
            std::memcpy(&bits,&f,sizeof(bits));
            collision_mesh::append32(bytes,bits);
        }
        for(auto i:h.indices)collision_mesh::append32(bytes,i);
    }
    return bytes;
}
inline std::optional<Set> decode(std::string const& bytes) {
    if(bytes.size()>max_bytes||bytes.size()<20||bytes.substr(0,4)!="NCH1")
        return std::nullopt;
    try {
        std::size_t offset=4;
        auto source_len=collision_mesh::read32(bytes,offset);
        Set set;
        set.splits_num=collision_mesh::read32(bytes,offset);
        auto count=collision_mesh::read32(bytes,offset);
        if(source_len==0||source_len>256||count==0||count>16||
           set.splits_num<count||offset+source_len>bytes.size())return std::nullopt;
        set.source=bytes.substr(offset,source_len);offset+=source_len;
        for(std::uint32_t i=0;i<count;++i) {
            auto verts=collision_mesh::read32(bytes,offset);
            auto indices=collision_mesh::read32(bytes,offset);
            if(verts<4||verts>128||indices<12||indices>2048||indices%3||
               offset+12ull*verts+4ull*indices>bytes.size())return std::nullopt;
            collision_cooker::Hull h;
            h.xyz.reserve(3*verts);h.indices.reserve(indices);
            for(std::uint32_t j=0;j<verts*3;++j) {
                auto bits=collision_mesh::read32(bytes,offset);
                float value;
                std::memcpy(&value,&bits,sizeof(value));
                h.xyz.push_back(value);
            }
            for(std::uint32_t j=0;j<indices;++j)
                h.indices.push_back(collision_mesh::read32(bytes,offset));
            set.hulls.push_back(std::move(h));
        }
        return offset==bytes.size()&&valid(set)?
            std::optional<Set>(std::move(set)):std::nullopt;
    }catch(...){return std::nullopt;}
}
inline std::optional<Set> lookup(Scene const& scene,std::uint64_t entity) {
    for(auto const& c:scene.components)
        if(c.entity_id==entity&&c.type==component_type&&c.version==version)
            return decode(c.payload);
    return std::nullopt;
}
inline void assign(Scene& scene,std::uint64_t entity,Set const& set) {
    if(!entity)throw std::invalid_argument("Invalid hull owner");
    SceneComponentBlob record{entity,component_type,version,encode(set)};
    for(auto& c:scene.components)
        if(c.entity_id==entity&&c.type==component_type) {
            c=std::move(record);return;
        }
    scene.components.push_back(std::move(record));
}
inline Set cook(collision_mesh::Mesh const& mesh,std::uint32_t splits,
                std::uint32_t resolution=100000,
                std::atomic<int>* progress=nullptr,
                std::atomic<bool>* cancellation=nullptr) {
    collision_cooker::Options opts;
    opts.max_hulls=splits;
    opts.resolution=resolution;
    opts.progress_percent=progress;
    opts.cancel_requested=cancellation;
    Set set{mesh.source,splits,collision_cooker::cook(mesh.xyz,mesh.indices,opts)};
    if(!valid(set))throw std::runtime_error("V-HACD result exceeds collision storage limits");
    return set;
}
inline std::vector<float> transformed(collision_cooker::Hull const& hull,
                SceneObject const& object,collision_authoring::Collider const& collider,
                bool body_local) {
    collision_mesh::Mesh mesh;
    mesh.xyz=hull.xyz;
    return collision_mesh::transform(mesh,object,collider,body_local);
}
} // namespace northstarcpp::collision_hulls
