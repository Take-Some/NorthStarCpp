#include <northstarcpp/physics.hpp>
#include <northstarcpp/physics_surface.hpp>
#include <northstarcpp/collision_mesh.hpp>
#include <northstarcpp/collision_authoring.hpp>
#include <stdexcept>
#include <iostream>
using namespace northstarcpp;
void check(bool b,char const* m){if(!b)throw std::runtime_error(m);}
std::vector<Vector3> vertices(float y){
    return {{-2,y,-1},{0,y,-1},{-2,y,1},{0,y,1},
            {0,y,-1},{2,y,-1},{0,y,1},{2,y,1}};
}
std::vector<std::uint32_t> triangles(){return {0,2,1,1,2,3,4,6,5,5,6,7};}
void hit(PhysicsScene& scene,float x,std::uint32_t material,std::uint32_t obj){
    auto result=scene.sweep_sphere({x,3,0},{x,0.75f,0},.12f);
    check(bool(result),"Missing Jolt surface hit");
    if(result->material_id!=material) std::cerr<<"wanted "<<material<<" got "<<result->material_id<<" tri "<<result->triangle_index<<" obj "<<result->object_index<<"\n";
    check(result->material_id==material,"Wrong triangle material");
    check(result->object_index==obj,"Wrong collider object");
    check(result->triangle_index<4,"Missing source triangle index");
    check(result->normal.y>.5f,"Wrong ground normal");
}
int main(){
try{
    Scene scene;
    scene.version=3;
    SceneObject dummy;dummy.name="Remote";dummy.node.id=10;
    dummy.position={100,0,100};scene.objects.push_back(dummy);
    PhysicsScene physics(scene);
    check(physics.provider_name()=="Jolt","Requires Jolt");
    check(physics.supports_surface_materials(),"Missing surface ABI");
    auto v=vertices(2.f);auto t=triangles();
    physics.add_static_mesh(v,t,{2,2,3,3});
    hit(physics,-1,physics_surface::grass,UINT32_MAX);
    hit(physics,1,physics_surface::metal,UINT32_MAX);
    bool rejected=false;
    try{physics.add_static_mesh(v,t,{1,2});}
    catch(std::invalid_argument const&){rejected=true;}
    check(rejected,"Accepted invalid material count");
    collision_mesh::Mesh mesh;
    mesh.source="mixed.obj";
    for(auto p:vertices(0))mesh.xyz.insert(mesh.xyz.end(),{p.x,p.y,p.z});
    mesh.indices=t;mesh.triangle_materials={8,8,5,5};
    auto encoded=collision_mesh::encode(mesh);
    check(encoded.substr(0,4)=="NCM2","Expected NCM2");
    auto decoded=collision_mesh::decode(encoded);
    check(decoded&&decoded->triangle_materials==mesh.triangle_materials,
          "NCM2 roundtrip");
    auto legacy=mesh;legacy.triangle_materials.clear();
    check(collision_mesh::decode(collision_mesh::encode(legacy)).has_value(),
          "NCM1 compatibility");
    Scene authored;authored.version=3;
    SceneObject floor;floor.name="Mixed";floor.node.id=101;
    floor.node.asset=mesh.source;floor.position={0,1,0};
    authored.objects.push_back(floor);
    collision_authoring::Collider c;
    c.shape=collision_authoring::Shape::ComplexMesh;
    collision_authoring::assign(authored,101,c);
    collision_mesh::assign(authored,101,mesh);
    validate_scene(authored);
    // Physical surface IDs must survive scene serialization, not just memory.
    auto scene_bytes=serialize_scene_v3(authored);
    auto restored_scene=parse_scene_v3(scene_bytes);
    auto persisted=collision_mesh::lookup(restored_scene,101);
    check(persisted && persisted->triangle_materials==mesh.triangle_materials,
          "NCM2 material IDs lost after scene persistence");
    PhysicsScene cooked(restored_scene);
    hit(cooked,-1,physics_surface::wood,0);
    hit(cooked,1,physics_surface::snow,0);
    std::cout<<"PASS Jolt triangle material IDs + NCM2 authoring\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
