// External geometry ABI fixture: deliberately different topology to prove override.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/geometry_plugin.h>
#include <cstring>
#include <cstdint>
namespace {
char const* error() {return "Geometry fixture failure";}
int generate(NorthStarGeometryRequest const* request,NorthStarGeometryCounts* counts,
             float* xyz,std::uint32_t xyz_capacity,
             std::uint16_t* indices,std::uint32_t index_capacity) {
    if(!request||request->size<sizeof(*request)||
       !counts||counts->size<sizeof(*counts))return 0;
    if(!xyz&&!indices&&xyz_capacity==0&&index_capacity==0) {
        counts->vertices=3;counts->indices=3;counts->stride=12;return 1;
    }
    if(!xyz||!indices||xyz_capacity<9||index_capacity<3)return 0;
    float const vertices[]={42.f,0.f,0.f,43.f,0.f,0.f,42.f,1.f,0.f};
    std::uint16_t const tris[]={0,1,2};
    std::memcpy(xyz,vertices,sizeof(vertices));
    std::memcpy(indices,tris,sizeof(tris));
    counts->vertices=3;counts->indices=3;counts->stride=12;
    return 1;
}
NorthStarGeometryApi const table{generate};
NorthStarModule const desc{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_GEOMETRY,
    "Fixture Geometry","1.0.0",
#ifdef NORTHSTARCPP_INVALID_GEOMETRY_FIXTURE
    999u,
#else
    NORTHSTARCPP_GEOMETRY_ABI,
#endif
    sizeof(table),&table,error
};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT NorthStarModule const* northstar_module_query(
    std::uint32_t host,char const* capability,std::uint32_t api) {
    if(host!=NORTHSTARCPP_MODULE_ABI || !capability ||
       std::strcmp(capability,NORTHSTARCPP_CAPABILITY_GEOMETRY)!=0) return nullptr;
    (void)api;
    return &desc;
}
