// Jolt provider collision shapes using pre-cooked V-HACD 4.1 convex pieces.
#pragma once
#include <northstarcpp/physics_plugin.h>
#include <northstarcpp/collision_cooker.hpp>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
namespace northstarcpp::jolt_mesh {
namespace j=JPH;
inline j::RefConst<j::Shape> build_hulls(
    const NorthStarPhysicsCookedHull* hulls,std::uint32_t count) {
    if(!hulls||count==0||count>16)
        throw std::invalid_argument("Invalid cooked hull count");
    j::StaticCompoundShapeSettings compound;
    j::RefConst<j::Shape> first;
    for(std::uint32_t i=0;i<count;++i) {
        auto const& h=hulls[i];
        if(!h.xyz||!h.indices||h.vertex_count<4||h.vertex_count>128||
           h.index_count<12||h.index_count>2048||h.index_count%3)
            throw std::invalid_argument("Invalid cooked hull topology");
        j::Array<j::Vec3> points;
        points.reserve(h.vertex_count);
        for(std::uint32_t k=0;k<h.vertex_count;++k) {
            auto const* xyz=h.xyz+3*k;
            if(!std::isfinite(xyz[0])||!std::isfinite(xyz[1])||
               !std::isfinite(xyz[2]))
                throw std::invalid_argument("Invalid cooked hull vertex");
            points.push_back(j::Vec3(xyz[0],xyz[1],xyz[2]));
        }
        for(std::uint32_t k=0;k<h.index_count;++k)
            if(h.indices[k]>=h.vertex_count)
                throw std::invalid_argument("Invalid cooked hull index");
        j::ConvexHullShapeSettings settings(points);
        auto created=settings.Create();
        if(created.HasError())
            throw std::runtime_error(std::string("Jolt convex hull failed: ")+
                                     created.GetError().c_str());
        auto const shape=created.Get();
        if(i==0)first=shape;
        compound.AddShape(j::Vec3::sZero(),j::Quat::sIdentity(),shape);
    }
    if(count==1)return first;
    auto combined=compound.Create();
    if(combined.HasError())
        throw std::runtime_error(std::string("Jolt convex compound failed: ")+
                                 combined.GetError().c_str());
    return combined.Get();
}
inline j::RefConst<j::Shape> build(
    const float* xyz,std::uint32_t vertices,const std::uint32_t* indices,
    std::uint32_t index_count,std::uint32_t mode,std::uint32_t splits) {
    if(!xyz||!indices||vertices<4||vertices>18000||
       index_count<12||index_count%3||index_count>54000||
       (mode!=3 && mode!=4)||splits<1||splits>16)
        throw std::invalid_argument("Invalid Jolt collision mesh request");
    for(std::uint32_t i=0;i<vertices*3;++i)
        if(!std::isfinite(xyz[i]))
            throw std::invalid_argument("Invalid Jolt collision vertex");
    for(std::uint32_t i=0;i<index_count;++i)
        if(indices[i]>=vertices)
            throw std::invalid_argument("Invalid Jolt collision index");
    if(mode==3) {
        j::TriangleList triangles;
        triangles.reserve(index_count/3);
        for(std::uint32_t i=0;i<index_count;i+=3) {
            j::Vec3 p[3];
            for(int n=0;n<3;++n) {
                auto const v=3*indices[i+n];
                p[n]=j::Vec3(xyz[v],xyz[v+1],xyz[v+2]);
            }
            triangles.emplace_back(p[0],p[1],p[2],0,i/3);
        }
        j::MeshShapeSettings settings(triangles);
        settings.mPerTriangleUserData=true;
        auto shape=settings.Create();
        if(shape.HasError())
            throw std::runtime_error(std::string("Jolt mesh cook failed: ")+
                                     shape.GetError().c_str());
        return shape.Get();
    }
    // Backward compatibility for old scenes without pre-cooked hull components:
    // use V-HACD, never the former triangle-bucket approximation.
    auto cooked=collision_cooker::cook(
        std::vector<float>(xyz,xyz+vertices*3),
        std::vector<std::uint32_t>(indices,indices+index_count),
        collision_cooker::Options{splits,100000,64,1.});
    std::vector<NorthStarPhysicsCookedHull> descriptors;
    descriptors.reserve(cooked.size());
    for(auto const& hull:cooked)
        descriptors.push_back({hull.xyz.data(),
            std::uint32_t(hull.xyz.size()/3),hull.indices.data(),
            std::uint32_t(hull.indices.size())});
    return build_hulls(descriptors.data(),std::uint32_t(descriptors.size()));
}
} // namespace northstarcpp::jolt_mesh
