// NorthStarCpp — material-preserving world-space HLOD geometry baker.
// Produces real indexed triangles, not AABB draw proxies. This is the
// aggregation stage; mesh simplification, texture atlases and GPU upload
// are separate stages and must not be implied by this API.
#pragma once
#include <northstarcpp/mesh_asset.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace northstarcpp::scene_hlod {

struct Source {
    MeshAsset const* mesh=nullptr;
    // 3x4 affine transform, row major, applied in world space.
    std::array<float,12> transform{1,0,0,0, 0,1,0,0, 0,0,1,0};
    std::string material_override;
    bool is_static=true;
};
struct BakeResult {
    MeshAsset mesh;
    std::size_t sources=0;
    std::size_t source_triangles=0;
};
namespace detail {
using Key=std::array<std::uint32_t,8>;
inline std::uint32_t bits(float value) noexcept {
    if(value==0.f)value=0.f; // canonicalize negative zero
    std::uint32_t result=0;
    std::memcpy(&result,&value,sizeof(result));
    return result;
}
inline Key key(MeshVertex const& v) noexcept {
    return {bits(v.position.x),bits(v.position.y),bits(v.position.z),
            bits(v.normal.x),bits(v.normal.y),bits(v.normal.z),
            bits(v.u),bits(v.v)};
}
struct Bucket {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::map<Key,std::uint32_t> unique;
    std::uint32_t add(MeshVertex const& v) {
        auto const k=key(v);
        auto const it=unique.find(k);
        if(it!=unique.end())return it->second;
        if(vertices.size()>=std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("HLOD vertex count exceeds index range");
        auto const id=static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(v);
        unique.emplace(k,id);
        return id;
    }
};
inline Vector3 transformed(Vector3 v,std::array<float,12> const& a) {
    return {a[0]*v.x+a[1]*v.y+a[2]*v.z+a[3],
            a[4]*v.x+a[5]*v.y+a[6]*v.z+a[7],
            a[8]*v.x+a[9]*v.y+a[10]*v.z+a[11]};
}
struct NormalMatrix {
    std::array<float,9> m{};
    float determinant=0.f;
    explicit NormalMatrix(std::array<float,12> const& t) {
        auto const a=t[0],b=t[1],c=t[2],d=t[4],e=t[5],f=t[6],
                   g=t[8],h=t[9],i=t[10];
        // Cofactor matrix = determinant * inverse-transpose.
        m={e*i-f*h, f*g-d*i, d*h-e*g,
           c*h-b*i, a*i-c*g, b*g-a*h,
           b*f-c*e, c*d-a*f, a*e-b*d};
        determinant=a*m[0]+b*m[1]+c*m[2];
        if(!std::isfinite(determinant) || std::abs(determinant)<1e-10f)
            throw std::invalid_argument("singular HLOD world transform");
    }
    Vector3 apply(Vector3 v) const {
        float const sign=determinant<0.f?-1.f:1.f;
        Vector3 n{sign*(m[0]*v.x+m[1]*v.y+m[2]*v.z),
                  sign*(m[3]*v.x+m[4]*v.y+m[5]*v.z),
                  sign*(m[6]*v.x+m[7]*v.y+m[8]*v.z)};
        float const length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
        if(!(length>1.e-10f) || !std::isfinite(length))
            throw std::invalid_argument("invalid HLOD transformed normal");
        return {n.x/length,n.y/length,n.z/length};
    }
};
}
inline BakeResult bake(std::vector<Source> const& sources,
                       std::size_t max_triangle_indices=3'000'000) {
    using BucketKey=std::pair<std::string,bool>;
    std::map<BucketKey,detail::Bucket> groups;
    BakeResult result;
    for(auto const& source:sources) {
        if(!source.is_static || !source.mesh)
            throw std::invalid_argument("HLOD bake requires a static mesh");
        for(float v:source.transform)
            if(!std::isfinite(v))
                throw std::invalid_argument("non-finite HLOD world transform");
        detail::NormalMatrix const normal(source.transform);
        auto const& mesh=*source.mesh;
        if(mesh.indices.size()%3!=0)
            throw std::invalid_argument("HLOD triangle list is malformed");
        auto append_section=[&](MeshSection const& section) {
            if(section.first_index>mesh.indices.size() ||
               section.index_count>mesh.indices.size()-section.first_index ||
               section.first_index%3 || section.index_count%3)
                throw std::invalid_argument("HLOD material range is invalid");
            auto const material=source.material_override.empty()?
                section.material_reference:source.material_override;
            auto& bucket=groups[{material,section.material_required}];
            for(std::size_t base=section.first_index;
                base<section.first_index+section.index_count;base+=3) {
                std::array<MeshVertex,3> triangle{};
                for(std::size_t corner=0;corner<3;++corner) {
                    auto const vertex=mesh.indices[base+corner];
                    if(vertex>=mesh.vertices.size())
                        throw std::invalid_argument("HLOD mesh has invalid index");
                    triangle[corner]=mesh.vertices[vertex];
                    triangle[corner].position=detail::transformed(
                        triangle[corner].position,source.transform);
                    triangle[corner].normal=normal.apply(triangle[corner].normal);
                    auto const& v=triangle[corner];
                    if(!std::isfinite(v.position.x)||!std::isfinite(v.position.y)||
                       !std::isfinite(v.position.z)||!std::isfinite(v.u)||
                       !std::isfinite(v.v))
                        throw std::invalid_argument("non-finite HLOD vertex");
                }
                if(normal.determinant<0.f)std::swap(triangle[1],triangle[2]);
                std::array<std::uint32_t,3> ids{};
                for(std::size_t corner=0;corner<3;++corner)
                    ids[corner]=bucket.add(triangle[corner]);
                if(ids[0]==ids[1]||ids[1]==ids[2]||ids[0]==ids[2])
                    continue; // no degenerate triangles after welding
                if(bucket.indices.size()+3>max_triangle_indices)
                    throw std::length_error("HLOD triangle budget exceeded");
                bucket.indices.insert(bucket.indices.end(),ids.begin(),ids.end());
                ++result.source_triangles;
            }
        };
        if(mesh.sections.empty())
            append_section({"","",0,mesh.indices.size(),false});
        else for(auto const& section:mesh.sections)append_section(section);
        ++result.sources;
    }

    bool first=true;
    for(auto& entry:groups) {
        auto& bucket=entry.second;
        if(bucket.indices.empty())continue;
        auto const base=static_cast<std::uint32_t>(result.mesh.vertices.size());
        result.mesh.sections.push_back({entry.first.first,entry.first.first,
            result.mesh.indices.size(),bucket.indices.size(),entry.first.second});
        for(auto const& v:bucket.vertices) {
            if(first) {
                result.mesh.minimum=result.mesh.maximum=v.position;
                first=false;
            } else {
                auto& lo=result.mesh.minimum;
                auto& hi=result.mesh.maximum;
                lo.x=std::min(lo.x,v.position.x);lo.y=std::min(lo.y,v.position.y);
                lo.z=std::min(lo.z,v.position.z);
                hi.x=std::max(hi.x,v.position.x);hi.y=std::max(hi.y,v.position.y);
                hi.z=std::max(hi.z,v.position.z);
            }
            result.mesh.vertices.push_back(v);
        }
        for(auto index:bucket.indices)
            result.mesh.indices.push_back(base+index);
    }
    return result;
}
} // namespace northstarcpp::scene_hlod
