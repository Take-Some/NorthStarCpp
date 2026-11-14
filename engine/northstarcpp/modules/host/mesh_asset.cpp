// NorthStarCpp - bounded Wavefront OBJ geometry parser
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/mesh_asset.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace northstarcpp {
namespace {
constexpr std::size_t max_vertices = 250000;
constexpr std::size_t max_triangles = 100000;
constexpr std::size_t max_source_bytes = 8 * 1024 * 1024;
bool finite(float value) { return std::isfinite(value) && std::abs(value) < 1000000.f; }
float number(std::string const& item) {
    std::size_t end=0;
    float result=std::stof(item,&end);
    if (end!=item.size() || !finite(result))
        throw std::invalid_argument("Invalid OBJ coordinate");
    return result;
}
std::size_t index(std::string const& item,std::size_t count) {
    if (item.empty()) throw std::invalid_argument("Missing OBJ index");
    std::size_t end=0;
    auto const raw=std::stoll(item,&end);
    if (end!=item.size() || raw==0 ||
        raw>static_cast<std::int64_t>(count) ||
        raw< -static_cast<std::int64_t>(count))
        throw std::out_of_range("Invalid OBJ vertex index");
    return static_cast<std::size_t>(
        raw>0 ? raw-1 : static_cast<std::int64_t>(count)+raw);
}
struct FaceIndex {
    std::size_t vertex, texture=0, normal=0;
    bool has_texture=false,has_normal=false;
};
FaceIndex face_index(std::string const& text,
                     std::size_t pos_count,std::size_t uv_count,std::size_t norm_count) {
    auto const first=text.find('/');
    if (first==std::string::npos)
        return {index(text,pos_count)};
    FaceIndex item{index(text.substr(0,first),pos_count)};
    auto const second=text.find('/',first+1);
    auto const uv=text.substr(first+1,
        second==std::string::npos ? std::string::npos : second-first-1);
    if (!uv.empty()) {
        item.texture=index(uv,uv_count);
        item.has_texture=true;
    }
    if (second!=std::string::npos) {
        if (text.find('/',second+1)!=std::string::npos)
            throw std::invalid_argument("Malformed OBJ face index");
        auto const vn=text.substr(second+1);
        if (!vn.empty()) {
            item.normal=index(vn,norm_count);
            item.has_normal=true;
        }
    }
    return item;
}
Vector3 cross(Vector3 a,Vector3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
Vector3 subtract(Vector3 a,Vector3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vector3 normalized(Vector3 v) {
    auto const size=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    if (!std::isfinite(size) || size<0.0000001f)
        throw std::invalid_argument("Degenerate OBJ triangle");
    return {v.x/size,v.y/size,v.z/size};
}
}
MeshAsset parse_obj_mesh(std::string const& source) {
    if (source.empty() || source.size()>max_source_bytes || source.find('\0')!=std::string::npos)
        throw std::invalid_argument("OBJ data exceeds limits");
    std::vector<Vector3> points, normals;
    std::vector<std::array<float,2>> uvs;
    MeshAsset asset{};
    std::istringstream input(source);
    std::string line;
    std::size_t line_count=0;
    while (std::getline(input,line)) {
        if (++line_count>1000000 || line.size()>8192)
            throw std::invalid_argument("OBJ line limits exceeded");
        auto const comment=line.find('#');
        if (comment!=std::string::npos) line.resize(comment);
        std::istringstream words(line);
        std::string op;
        if (!(words>>op))continue;
        if (op=="v" || op=="vn") {
            std::string a,b,c,extra;
            if (!(words>>a>>b>>c) || (words>>extra && op=="vn"))
                throw std::invalid_argument("Invalid OBJ position/normal");
            Vector3 value{number(a),number(b),number(c)};
            if (op=="v") {
                if(points.size()>=max_vertices)
                    throw std::invalid_argument("OBJ vertex limit exceeded");
                points.push_back(value);
            } else {
                if(normals.size()>=max_vertices)
                    throw std::invalid_argument("OBJ normal limit exceeded");
                normals.push_back(value);
            }
        } else if (op=="vt") {
            std::string a,b;
            if (!(words>>a>>b))throw std::invalid_argument("Invalid OBJ texture coordinates");
            if (uvs.size()>=max_vertices) throw std::invalid_argument("OBJ UV limit exceeded");
            uvs.push_back({number(a),number(b)});
        } else if (op=="f") {
            std::vector<FaceIndex> face;
            std::string part;
            while(words>>part) {
                if(face.size()==128)
                    throw std::invalid_argument("OBJ polygon exceeds 128 corners");
                face.push_back(face_index(part,points.size(),uvs.size(),normals.size()));
            }
            if(face.size()<3) throw std::invalid_argument("OBJ face has fewer than 3 vertices");
            for(std::size_t i=1;i+1<face.size();++i) {
                if (asset.indices.size()/3>=max_triangles)
                    throw std::invalid_argument("OBJ triangle limit exceeded");
                std::array<FaceIndex,3> tri{face[0],face[i],face[i+1]};
                auto const normal=normalized(cross(
                    subtract(points[tri[1].vertex],points[tri[0].vertex]),
                    subtract(points[tri[2].vertex],points[tri[0].vertex])));
                for(auto const& item:tri) {
                    MeshVertex v{};
                    v.position=points[item.vertex];
                    v.normal=item.has_normal ? normals[item.normal] : normal;
                    if(item.has_texture) {
                        v.u=uvs[item.texture][0];v.v=uvs[item.texture][1];
                    }
                    asset.indices.push_back(static_cast<std::uint32_t>(asset.vertices.size()));
                    asset.vertices.push_back(v);
                }
            }
        } else if (op=="o" || op=="g" || op=="s" || op=="usemtl" || op=="mtllib") {
            // Metadata is preserved in source; PBR/material import is a separate layer.
        } else {
            throw std::invalid_argument("Unsupported OBJ record: " + op);
        }
    }
    if(asset.vertices.empty())throw std::invalid_argument("OBJ has no triangles");
    asset.minimum=asset.maximum=asset.vertices.front().position;
    for(auto const& v:asset.vertices) {
        asset.minimum.x=std::min(asset.minimum.x,v.position.x);
        asset.minimum.y=std::min(asset.minimum.y,v.position.y);
        asset.minimum.z=std::min(asset.minimum.z,v.position.z);
        asset.maximum.x=std::max(asset.maximum.x,v.position.x);
        asset.maximum.y=std::max(asset.maximum.y,v.position.y);
        asset.maximum.z=std::max(asset.maximum.z,v.position.z);
    }
    return asset;
}
} // namespace northstarcpp
