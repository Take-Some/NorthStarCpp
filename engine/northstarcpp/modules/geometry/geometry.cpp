// Native engine.geometry provider backed directly by the original
// foundation/core/sources/geometry_utils.cpp (libfoundation_core).
// Builtin and external provider use the same source and stable C ABI.
// The provider compiles directly against the real foundation types. The
// stable C ABI below is the *boundary*, not an implementation/compat shim.
#ifndef NORTHSTARCPP_STANDALONE_MATH
#define NORTHSTARCPP_STANDALONE_MATH 1
#endif
#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#define NORTHSTARCPP_STATIC_LIBRARIES 1
#endif
#include <northstarcpp/core/math_standalone.h>
#include <northstarcpp/geometry_utils.h>
#include <northstarcpp/geometry_plugin.h>
#include <northstarcpp/geometry.hpp>
#include "module_provider.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using northstarcpp::module_provider::guarded;
void require(bool valid,char const* why) {
    if(!valid)throw std::invalid_argument(why);
}
using Mesh=northstarcpp::geometry::Mesh;

// Convert only at the plugin ABI boundary. All tessellation is performed
// by the ORIGINAL implementation in foundation/core/sources/geometry_utils.cpp.
Mesh original_geometry(NorthStarGeometryRequest const& req) {
    using namespace northstarcpp;
    using namespace northstarcpp::geometry_utils;
    geom_vertices_type vertices;
    geom_indices_type indices;
    math::float4x4 transform;
    transform.identity();
    bool ok=false;
    if(req.kind==NORTHSTAR_GEOMETRY_SOLID) {
        switch(req.shape) {
        case 0: ok=create_cube(vertices,indices,transform,{1.f,1.f,1.f});break;
        case 2: ok=create_cone(vertices,indices,transform,{1.f,1.f,1.f});break;
        case 3: ok=create_cylinder(vertices,indices,transform,{1.f,1.f,1.f});break;
        case 4: ok=create_ellipsoid(vertices,indices,transform,{1.f,1.f,1.f});break;
        default:break; // rectangle has only an original immutable primitive
        }
    } else if(req.kind==NORTHSTAR_GEOMETRY_RING) {
        ok=create_ring(vertices,indices,req.a,req.b,
                       static_cast<std::uint16_t>(req.u));
    } else if(req.kind==NORTHSTAR_GEOMETRY_TORUS) {
        ok=create_torus(vertices,indices,transform,req.a,req.b,
                        static_cast<std::uint16_t>(req.u),
                        static_cast<std::uint16_t>(req.v));
    }
    require(ok,"Original foundation geometry generation failed");
    Mesh result;
    result.vertices.assign(vertices.begin(),vertices.end());
    result.indices.assign(indices.begin(),indices.end());
    return result;
}

Mesh source_geometry(NorthStarGeometryRequest const& req) {
    using namespace northstarcpp;
    using namespace geometry;
    require(req.size>=sizeof(NorthStarGeometryRequest),"Bad geometry request size");
    require(req.flags==0,"Geometry flags are reserved");
    require(std::isfinite(req.a)&&std::isfinite(req.b),"Non-finite geometry dimensions");
    math::float4x4 identity;
    identity.identity();
    if(req.kind==NORTHSTAR_GEOMETRY_SOLID) {
        require(req.shape<=4 && req.u==0 && req.v==0,
                "Invalid solid primitive parameters");
        // The original library provides geometry for four solids; the
        // rectangle is stored as immutable original foundation vertices.
        if(req.shape==1)
            return make_mesh(SolidPrimitive::rectangle,{1.f,1.f,1.f},identity);
        return original_geometry(req);
    }
    if(req.kind==NORTHSTAR_GEOMETRY_WIRE) {
        require(req.shape<=4 && req.u==0 && req.v==0,
                "Invalid wire primitive parameters");
        // Original foundation wire primitives are already immutable arrays.
        auto const view=primitive(static_cast<WirePrimitive>(req.shape));
        Mesh result;
        result.vertices.reserve(view.vertex_count);
        for(std::size_t i=0;i<view.vertex_count;++i) {
            auto const* coord=view.positions+i*3u;
            result.vertices.emplace_back(coord[0],coord[1],coord[2]);
        }
        result.indices.assign(view.indices,view.indices+view.index_count);
        return result;
    }
    require(req.shape==0,"Nonzero generated geometry shape");
    if(req.kind==NORTHSTAR_GEOMETRY_RING) {
        // Legacy source uses 16-bit indices; bound before invoking asserts.
        require(req.a>0.f && req.b>0.f && req.u>=4 && req.u<=32768 &&
                req.v==0,"Invalid original ring dimensions or tessellation");
        return original_geometry(req);
    }
    if(req.kind==NORTHSTAR_GEOMETRY_TORUS) {
        require(req.a>req.b && req.b>0.f &&
                req.u>=3 && req.u<=4096 &&
                req.v>=3 && req.v<=4096,
                "Invalid original torus dimensions or tessellation");
        require(static_cast<std::uint64_t>(req.u)*req.v<=65535,
                "Original torus exceeds 16-bit vertex capacity");
        return original_geometry(req);
    }
    throw std::invalid_argument("Unknown geometry kind");
}
// A size-only ABI query is O(1): it MUST NOT tessellate high-resolution
// terrain primitives only to discard their allocations and generated data.
std::pair<std::size_t,std::size_t> original_geometry_counts(
    NorthStarGeometryRequest const& req) {
    using namespace northstarcpp::geometry;
    require(req.size>=sizeof(NorthStarGeometryRequest) && req.flags==0,
            "Invalid geometry request");
    require(std::isfinite(req.a)&&std::isfinite(req.b),
            "Non-finite geometry parameters");
    if(req.kind==NORTHSTAR_GEOMETRY_SOLID ||
       req.kind==NORTHSTAR_GEOMETRY_WIRE) {
        require(req.shape<=4 && req.u==0 && req.v==0,
                "Invalid original primitive shape");
        auto const view=req.kind==NORTHSTAR_GEOMETRY_SOLID?
            primitive(static_cast<SolidPrimitive>(req.shape)):
            primitive(static_cast<WirePrimitive>(req.shape));
        return {view.vertex_count,view.index_count};
    }
    require(req.shape==0,"Unexpected generated geometry shape");
    if(req.kind==NORTHSTAR_GEOMETRY_RING) {
        require(req.a>0.f && req.b>0.f && req.v==0 &&
                req.u>=4 && req.u<=32768,
                "Invalid original ring parameters");
        return {std::size_t(req.u)*2u,std::size_t(req.u)*6u};
    }
    if(req.kind==NORTHSTAR_GEOMETRY_TORUS) {
        require(req.a>req.b && req.b>0.f &&
                req.u>=3 && req.u<=4096 && req.v>=3 && req.v<=4096,
                "Invalid original torus parameters");
        std::size_t const count=std::size_t(req.u)*req.v;
        require(count<=65535,"Original torus exceeds 16-bit index capacity");
        return {count,count*6u};
    }
    throw std::invalid_argument("Unsupported original geometry category");
}
int generate(NorthStarGeometryRequest const* request,
             NorthStarGeometryCounts* counts,
             float* xyz,std::uint32_t xyz_capacity,
             std::uint16_t* indices,std::uint32_t index_capacity) {
    return guarded([&]{
        require(request&&counts,"Missing geometry request or counts");
        // Check both public structure sizes before reading the remaining fields.
        require(request->size>=sizeof(NorthStarGeometryRequest) &&
                counts->size>=sizeof(NorthStarGeometryCounts),
                "Unsupported geometry ABI structure size");
        auto const estimated=original_geometry_counts(*request);
        auto const vertices=estimated.first;
        auto const indices_count=estimated.second;
        require(vertices>0 && vertices<=NORTHSTARCPP_GEOMETRY_MAX_VERTICES &&
                indices_count>0 && indices_count<=NORTHSTARCPP_GEOMETRY_MAX_INDICES,
                "Geometry output exceeds ABI limits");
        std::uint32_t const nv=static_cast<std::uint32_t>(vertices);
        std::uint32_t const ni=static_cast<std::uint32_t>(indices_count);
        bool const querying=!xyz&&!indices&&xyz_capacity==0&&index_capacity==0;
        if(querying) {
            counts->vertices=nv;
            counts->indices=ni;
            counts->stride=3u*sizeof(float);
            return; // No materialization, allocations or source tessellation.
        }
        require(xyz && indices && xyz_capacity>=nv*3u &&
                index_capacity>=ni,"Insufficient geometry output buffer");
        Mesh geometry=source_geometry(*request);
        require(geometry.vertices.size()==vertices &&
                geometry.indices.size()==indices_count,
                "Original source geometry topology unexpectedly changed");
        for(auto const id:geometry.indices)
            require(id<vertices,"Corrupt original primitive indices");
        for(auto const& vertex:geometry.vertices)
            require(std::isfinite(vertex.x)&&std::isfinite(vertex.y)&&
                    std::isfinite(vertex.z),"Non-finite generated coordinate");
        counts->vertices=nv;
        counts->indices=ni;
        counts->stride=3u*sizeof(float);
        for(std::size_t i=0;i<vertices;++i) {
            xyz[i*3u+0]=geometry.vertices[i].x;
            xyz[i*3u+1]=geometry.vertices[i].y;
            xyz[i*3u+2]=geometry.vertices[i].z;
        }
        std::copy(geometry.indices.begin(),geometry.indices.end(),indices);
    });
}
NorthStarGeometryApi const api{generate};
NorthStarModule const module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_GEOMETRY,"NorthStar Geometry","1.1.0",
    NORTHSTARCPP_GEOMETRY_ABI,sizeof(api),&api,
    northstarcpp::module_provider::last_error};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT NorthStarModule const* northstar_module_query(
    std::uint32_t host,char const* capability,std::uint32_t version) {
    return northstarcpp::module_provider::match(module,host,capability,version);
}
