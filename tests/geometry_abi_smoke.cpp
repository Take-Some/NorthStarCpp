// Uses the real foundation-backed geometry implementation via plugin dispatch.
#include <northstarcpp/geometry_service.hpp>
#include <northstarcpp/geometry_scene.hpp>
#include <northstarcpp/scene_component_registry.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace northstarcpp;

static void require(bool ok, char const* text) {
    if(!ok)throw std::runtime_error(text);
}
int main() {
    try {
        northstarcpp::GeometryService geometry;
        if(geometry.provider_name()!="NorthStar Geometry") {
            std::cerr<<"Expected NorthStar Geometry but selected "
                     <<geometry.provider_name()<<" from "<<geometry.provider_version()<<'\n';
            return 4;
        }
        auto const cube=geometry.solid(0);
        require(cube.xyz.size()==24 && cube.indices.size()==36,"Cube topology changed");
        auto const wire=geometry.wire(0);
        require(!wire.indices.empty() && wire.indices.size()%2==0,"Wire geometry missing");
        auto const ring=geometry.ring(1.f,0.5f,12);
        require(ring.xyz.size()==72 && ring.indices.size()==72,"Original ring tessellation changed");
        auto const torus=geometry.torus(3.f,0.5f,16,16);
        require(torus.xyz.size()==768 && torus.indices.size()==1536,
                "Original torus tessellation changed");
        for(auto const& mesh:{cube,wire,ring,torus}) {
            for(auto f:mesh.xyz)require(std::isfinite(f),"Non-finite foundation geometry");
            for(auto id:mesh.indices)
                require(id<mesh.xyz.size()/3u,"Corrupt foundation geometry indices");
        }
        auto expect_invalid=[&](NorthStarGeometryRequest r) {
            bool rejected=false;
            try { (void)geometry.generate(r); }
            catch(std::exception const&) {rejected=true;}
            require(rejected,"Malformed geometry request was not rejected");
        };
        expect_invalid({sizeof(NorthStarGeometryRequest),NORTHSTAR_GEOMETRY_RING,0,0,
                        1.f,0.5f,2,0});
        expect_invalid({sizeof(NorthStarGeometryRequest),NORTHSTAR_GEOMETRY_TORUS,0,0,
                        3.f,0.5f,257,256});
        expect_invalid({sizeof(NorthStarGeometryRequest),NORTHSTAR_GEOMETRY_SOLID,999,0,
                        0.f,0.f,0,0});
        expect_invalid({4,NORTHSTAR_GEOMETRY_SOLID,0,0,0,0,0,0});
        register_geometry_primitive_component();
        require(scene_component_schema(geometry_primitive_component).has_value(),
                "Native foundation component schema is not visible to the Editor");
        auto const torusRequest=NorthStarGeometryRequest{
            sizeof(NorthStarGeometryRequest),NORTHSTAR_GEOMETRY_TORUS,
            0,0,.65f,.24f,24,12};
        auto component=make_geometry_primitive_component(17,torusRequest);
        NorthStarGeometryRequest decoded{};
        require(decode_geometry_primitive_component(component,decoded) &&
                decoded.u==24 && decoded.v==12 && decoded.a==.65f,
                "Typed procedural mesh component roundtrip failed");
        require(validate_scene_component_blob(component),
                "Component inspector provider rejected authored geometry");
        Scene source;
        source.version=3;
        source.name="NorthStar geometry gameplay scene";
        SceneObject object;
        object.name="Torus";
        object.node.id=17;
        source.objects.push_back(object);
        source.components.push_back(component);
        validate_scene(source);
        auto const bytes=serialize_scene_binary(source);
        auto readback=parse_scene_binary(bytes);
        require(readback.components.size()==1 &&
                readback.components.front().payload==component.payload &&
                readback.components.front().entity_id==17,
                "NEF8 failed to preserve the native geometry primitive");
        auto malformed=component;
        malformed.payload.pop_back();
        require(!validate_scene_component_blob(malformed),
                "Malformed geometry component passed the schema validator");
        return 0;
    } catch(std::exception const& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
