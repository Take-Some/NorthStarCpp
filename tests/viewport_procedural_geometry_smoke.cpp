#include "../engine/northstarcpp/modules/viewport/viewport_procedural_geometry.hpp"
#include <stdexcept>
#include <vector>
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(false)
int main(){
    northstarcpp::SceneObject object{};
    object.node.id=77;object.position={10.f,2.f,5.f};object.size=2.f;
    NorthStarRenderCube cube{};
    cube.rotation[0]=cube.rotation[4]=cube.rotation[8]=1.f;
    northstarcpp::GeometryData geometry;
    geometry.xyz={0.f,0.f,0.f,1.f,0.f,0.f,0.f,0.f,1.f};
    geometry.indices={0,1,2};
    std::vector<NorthStarMaterialVertex> vertices;
    std::vector<NorthStarRenderPickVertex> picks;
    bool complete=true;
    northstarcpp::viewport_detail::append_procedural_geometry(
        vertices,geometry,object,cube,picks,true,77,complete);
    CHECK(vertices.size()==3 && picks.size()==3 && complete);
    CHECK(vertices[0].x==10.f && vertices[0].y==2.f && vertices[0].z==5.f);
    CHECK(vertices[1].x==12.f && vertices[2].z==7.f);
    CHECK(vertices[1].u==1.f && vertices[2].v==1.f);
    CHECK(vertices[0].ny==-1.f && picks[0].entity_id==77);
    vertices.clear();picks.clear();complete=false;
    northstarcpp::viewport_detail::append_procedural_geometry(
        vertices,geometry,object,cube,picks,true,77,complete);
    CHECK(vertices.size()==3 && picks.empty());
    geometry.indices={0,1,9};
    bool rejected=false;
    try{northstarcpp::viewport_detail::append_procedural_geometry(
        vertices,geometry,object,cube,picks,false,77,complete);}
    catch(std::invalid_argument const&){rejected=true;}
    CHECK(rejected);
    geometry.indices={0,1};rejected=false;
    try{northstarcpp::viewport_detail::append_procedural_geometry(
        vertices,geometry,object,cube,picks,false,77,complete);}
    catch(std::invalid_argument const&){rejected=true;}
    CHECK(rejected);
}
