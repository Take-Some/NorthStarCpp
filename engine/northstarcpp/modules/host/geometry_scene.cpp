// Versioned binary scene component for the native engine.geometry provider.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/geometry_scene.hpp>
#include <northstarcpp/scene_component_registry.hpp>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace northstarcpp {
namespace {
void put32(std::string& bytes,std::uint32_t bits) {
    for(unsigned shift=0;shift<32;shift+=8)
        bytes.push_back(static_cast<char>((bits>>shift)&0xff));
}
std::uint32_t get32(std::string const& bytes,std::size_t i) {
    std::uint32_t bits=0;
    for(unsigned j=0;j<4;++j)
        bits|=static_cast<std::uint32_t>(
            static_cast<unsigned char>(bytes[i+j]))<<(j*8);
    return bits;
}
void put_float(std::string& bytes,float value) {
    std::uint32_t bits=0;
    static_assert(sizeof(bits)==sizeof(value),"float32 required");
    std::memcpy(&bits,&value,sizeof(bits));
    put32(bytes,bits);
}
float get_float(std::string const& bytes,std::size_t i) {
    std::uint32_t bits=get32(bytes,i);
    float value=0;
    std::memcpy(&value,&bits,sizeof(value));
    return value;
}
bool valid_request(NorthStarGeometryRequest const& request) {
    if(request.size<sizeof(NorthStarGeometryRequest) || request.flags!=0 ||
       !std::isfinite(request.a) || !std::isfinite(request.b))
        return false;
    if(request.kind==NORTHSTAR_GEOMETRY_SOLID ||
       request.kind==NORTHSTAR_GEOMETRY_WIRE)
        return request.shape<=4 && request.u==0 && request.v==0;
    if(request.shape!=0)return false;
    if(request.kind==NORTHSTAR_GEOMETRY_RING)
        return request.a>0 && request.b>0 && request.v==0 &&
               request.u>=3 && request.u<=32768;
    if(request.kind==NORTHSTAR_GEOMETRY_TORUS)
        return request.a>0 && request.b>0 &&
               request.u>=3 && request.v>=3 &&
               static_cast<std::uint64_t>(request.u)*request.v<=65535;
    return false;
}
bool validate_payload(void*,std::uint32_t version,
                      void const* payload,std::size_t length) {
    if(version!=geometry_primitive_schema || length!=geometry_primitive_payload_bytes ||
       !payload)return false;
    SceneComponentBlob v;
    v.type=geometry_primitive_component;
    v.version=version;
    v.payload.assign(static_cast<char const*>(payload),length);
    try {
        NorthStarGeometryRequest request{};
        return decode_geometry_primitive_component(v,request);
    } catch(...) {return false;}
}
}
void register_geometry_primitive_component() {
    static std::once_flag once;
    std::call_once(once,[]{
        if(!register_scene_component_type({
            geometry_primitive_component,geometry_primitive_schema,
            geometry_primitive_schema,validate_payload,nullptr}))
            throw std::runtime_error("Cannot register engine.geometry.primitive");
    });
}
SceneComponentBlob make_geometry_primitive_component(
    std::uint64_t entity_id,NorthStarGeometryRequest const& request) {
    register_geometry_primitive_component();
    if(!entity_id || !valid_request(request))
        throw std::invalid_argument("Invalid scene geometry primitive");
    SceneComponentBlob component;
    component.entity_id=entity_id;
    component.type=geometry_primitive_component;
    component.version=geometry_primitive_schema;
    component.payload.reserve(geometry_primitive_payload_bytes);
    put32(component.payload,request.kind);
    put32(component.payload,request.shape);
    put_float(component.payload,request.a);
    put_float(component.payload,request.b);
    put32(component.payload,request.u);
    put32(component.payload,request.v);
    return component;
}
bool decode_geometry_primitive_component(SceneComponentBlob const& component,
                                          NorthStarGeometryRequest& request) {
    if(component.type!=geometry_primitive_component)return false;
    if(component.version!=geometry_primitive_schema ||
       component.payload.size()!=geometry_primitive_payload_bytes)
        throw std::invalid_argument("Unsupported procedural geometry component");
    auto const& bytes=component.payload;
    NorthStarGeometryRequest next{};
    next.size=sizeof(next);
    next.kind=get32(bytes,0);
    next.shape=get32(bytes,4);
    next.a=get_float(bytes,8);
    next.b=get_float(bytes,12);
    next.u=get32(bytes,16);
    next.v=get32(bytes,20);
    if(!valid_request(next))
        throw std::invalid_argument("Malformed scene geometry parameters");
    request=next;
    return true;
}
} // namespace northstarcpp
