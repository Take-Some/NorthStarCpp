// engine.geometry host bridge: all geometry generation dispatches via Module.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/geometry_service.hpp>
#include "module_loader.hpp"
#include <stdexcept>

namespace northstarcpp {
struct GeometryService::State {
    Module module{NORTHSTARCPP_CAPABILITY_GEOMETRY};
    NorthStarGeometryApi const& api=
        *module.query<NorthStarGeometryApi>(NORTHSTARCPP_GEOMETRY_ABI);
};
GeometryService::GeometryService():state_(std::make_unique<State>()){}
GeometryService::~GeometryService()=default;
std::string GeometryService::provider_name() const {return state_->module.provider();}
std::string GeometryService::provider_version() const {return state_->module.version();}
GeometryData GeometryService::generate(NorthStarGeometryRequest const& request) const {
    if(request.size<sizeof(NorthStarGeometryRequest))
        throw std::invalid_argument("Bad geometry request size");
    NorthStarGeometryCounts counts{sizeof(NorthStarGeometryCounts),0,0,0};
    auto const& api=state_->api;
    if(!api.generate(&request,&counts,nullptr,0,nullptr,0))
        throw state_->module.error("Cannot query geometry output size");
    if(counts.stride!=sizeof(float)*3 ||
       counts.vertices==0 || counts.vertices>NORTHSTARCPP_GEOMETRY_MAX_VERTICES ||
       counts.indices==0 || counts.indices>NORTHSTARCPP_GEOMETRY_MAX_INDICES)
        throw std::runtime_error("Geometry provider returned invalid buffer lengths");
    GeometryData result;
    result.vertex_stride=counts.stride;
    result.xyz.resize(static_cast<std::size_t>(counts.vertices)*3u);
    result.indices.resize(counts.indices);
    NorthStarGeometryCounts produced{sizeof(NorthStarGeometryCounts),0,0,0};
    if(!api.generate(&request,&produced,result.xyz.data(),
                     static_cast<std::uint32_t>(result.xyz.size()),
                     result.indices.data(),
                     static_cast<std::uint32_t>(result.indices.size())))
        throw state_->module.error("Cannot generate geometry mesh");
    if(produced.vertices!=counts.vertices ||
       produced.indices!=counts.indices || produced.stride!=counts.stride)
        throw std::runtime_error("Geometry provider changed output layout");
    for(auto id:result.indices)
        if(id>=counts.vertices)
            throw std::runtime_error("Geometry provider returned an invalid index");
    return result;
}
GeometryData GeometryService::solid(std::uint32_t shape) const {
    return generate({sizeof(NorthStarGeometryRequest),NORTHSTAR_GEOMETRY_SOLID,
        shape,0,0.f,0.f,0,0});
}
GeometryData GeometryService::wire(std::uint32_t shape) const {
    return generate({sizeof(NorthStarGeometryRequest),NORTHSTAR_GEOMETRY_WIRE,
        shape,0,0.f,0.f,0,0});
}
GeometryData GeometryService::ring(float inner,float width,std::uint32_t segs) const {
    return generate({sizeof(NorthStarGeometryRequest),NORTHSTAR_GEOMETRY_RING,
        0,0,inner,width,segs,0});
}
GeometryData GeometryService::torus(float major,float minor,std::uint32_t a,
                                    std::uint32_t b) const {
    return generate({sizeof(NorthStarGeometryRequest),NORTHSTAR_GEOMETRY_TORUS,
        0,0,major,minor,a,b});
}
} // namespace northstarcpp
