// Proves engine.geometry builtin/override output originates in the original
// native foundation C++ algorithms, not merely an equivalent reimplementation.
// Runs unchanged with both bundled provider and external DLL provider.
#include <northstarcpp/core/math_standalone.h>
#include <northstarcpp/geometry_utils.h>
#include <northstarcpp/geometry_service.hpp>
#include <northstarcpp/geometry_plugin.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace northstarcpp;

namespace {
void verify(std::uint32_t kind,std::uint32_t shape,float a,float b,
            std::uint32_t u,std::uint32_t v) {
    NorthStarGeometryRequest const request{
        sizeof(NorthStarGeometryRequest),kind,shape,0,a,b,u,v};
    geometry_utils::geom_vertices_type original;
    geometry_utils::geom_indices_type originalIndices;
    math::float4x4 transform;
    transform.identity();
    bool accepted=false;
    if(kind==NORTHSTAR_GEOMETRY_SOLID) {
        switch(shape) {
            case 0:accepted=geometry_utils::create_cube(
                original,originalIndices,transform,{1.f,1.f,1.f});break;
            case 2:accepted=geometry_utils::create_cone(
                original,originalIndices,transform,{1.f,1.f,1.f});break;
            case 3:accepted=geometry_utils::create_cylinder(
                original,originalIndices,transform,{1.f,1.f,1.f});break;
            case 4:accepted=geometry_utils::create_ellipsoid(
                original,originalIndices,transform,{1.f,1.f,1.f});break;
        }
    } else if(kind==NORTHSTAR_GEOMETRY_RING) {
        accepted=geometry_utils::create_ring(
            original,originalIndices,a,b,static_cast<std::uint16_t>(u));
    } else if(kind==NORTHSTAR_GEOMETRY_TORUS) {
        accepted=geometry_utils::create_torus(
            original,originalIndices,transform,a,b,
            static_cast<std::uint16_t>(u),static_cast<std::uint16_t>(v));
    }
    if(!accepted)throw std::runtime_error("Original source refused test geometry");

    GeometryService service;
    auto const generated=service.generate(request);
    if(generated.xyz.size()!=original.size()*3 ||
       generated.indices.size()!=originalIndices.size())
        throw std::runtime_error("ABI provider changed original mesh topology");
    for(std::size_t i=0;i<original.size();++i) {
        float const xyz[3]{original[i].x,original[i].y,original[i].z};
        // Bitwise identical float results prove the provider did not
        // synthesize or approximate the geometry via separate algorithms.
        if(std::memcmp(xyz,generated.xyz.data()+i*3,sizeof(xyz))!=0)
            throw std::runtime_error("ABI provider geometry differs from foundation source");
    }
    if(!std::equal(originalIndices.begin(),originalIndices.end(),
                   generated.indices.begin()))
        throw std::runtime_error("ABI provider reindexed original geometry");
}
}
int main() {
    try {
        for(auto kind:{0u,2u,3u,4u})
            verify(NORTHSTAR_GEOMETRY_SOLID,kind,0.f,0.f,0,0);
        for(auto segments:{4u,8u,32u,256u})
            verify(NORTHSTAR_GEOMETRY_RING,0,1.2f,.8f,segments,0);
        for(auto const segments:{8u,16u,48u})
            verify(NORTHSTAR_GEOMETRY_TORUS,0,3.f,.7f,segments,segments);
        std::cout<<"Original foundation mesh parity: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
