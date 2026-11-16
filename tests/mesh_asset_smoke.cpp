// NorthStarCpp Wavefront OBJ / Scene V3 mesh smoke.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/native_model.hpp>
#include <northstarcpp/assets.hpp>
#include <fstream>
#include <sstream>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace northstarcpp;
namespace {
void require(bool condition,const char* problem) {
    if(!condition) throw std::runtime_error(problem);
}
void reject(std::function<void()> f) {
    bool rejected=false;
    try { f(); } catch(std::exception const&) {rejected=true;}
    require(rejected,"Malformed OBJ was accepted");
}
std::string quad=R"OBJ(
# two triangles from a four-sided polygon
v -1 0 -1
v 1 0 -1
v 1 0 1
v -1 0 1
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 1 0
f 1/1/1 2/2/1 3/3/1 4/4/1
)OBJ";
}
int main(int argc,char** argv) {
    try {
        auto mesh=parse_obj_mesh(quad);
        require(mesh.vertices.size()==6 && mesh.indices.size()==6,"Quad triangulation");
        require(std::abs(mesh.minimum.x+1.f)<0.0001f &&
                std::abs(mesh.maximum.z-1.f)<0.0001f,"Mesh bounds");
        require(mesh.vertices[0].normal.y==1.f &&
                mesh.vertices[4].v==1.f,"OBJ normal and UV decoding");
        auto negative=parse_obj_mesh(R"OBJ(
v 0 0 0
v 1 0 0
v 0 1 0
f -3 -2 -1
)OBJ");
        require(negative.indices.size()==3 && negative.vertices[1].position.x==1.f,
                "Negative vertex indices");
        reject([]{parse_obj_mesh("v 0 0 0\nv 1 0 0\nf 0 1 2\n");});
        reject([]{parse_obj_mesh("v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n");});
        reject([]{parse_obj_mesh("v nan 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");});
        reject([]{parse_obj_mesh("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 4\n");});
        reject([]{parse_obj_mesh(std::string(8*1024*1024+1,'x'));});
        if(argc==4 && std::string(argv[1])=="--mt28") {
            AssetStore assets;
            assets.mount_project_assets(argv[2],argv[3]);
            static constexpr char body[]=
                "models/mahir_mt28/v_mahir_mt28_coach__ext01_body_01.ydr";
            std::cerr<<"MT28 basic mesh loading..."<<std::endl;
            auto const basic=load_scene_mesh(assets,body);
            std::cerr<<"MT28 basic geometry "<<basic.vertices.size()<<std::endl;
            auto const native=load_native_model_resource(assets,body,true);
            require(native.mesh.vertices.size()>600000,"MT28 body geometry missing");
            require(native.mesh.indices.size()>2000000,"MT28 body triangles missing");
            require(native.mesh.sections.size()>=30,"MT28 material groups missing");
            std::cout<<"MT28 native geometry PASS: "<<native.mesh.vertices.size()
                <<" vertices "<<native.mesh.indices.size()/3<<" triangles "
                <<native.mesh.sections.size()<<" material slots; unresolved "
                <<native.unresolved_materials.size()<<'\n';
            return 0;
        }
        if(argc==3) {
            std::ifstream input(argv[1],std::ios::binary);
            require(input.good(),"Missing actual OBJ lab asset");
            std::ostringstream data;
            data<<input.rdbuf();
            auto lab=parse_obj_mesh(data.str());
            require(lab.vertices.size()>=6 && lab.indices.size()%3==0,
                    "Actual mesh is not triangulated");
            auto scene=load_scene(argv[2]);
            require(scene.version==3,"Lab scene not V3");
            bool found=false;
            for(auto const& actor:scene.objects) {
                if(actor.node.asset=="assets/models/pedestal.obj") found=true;
            }
            require(found,"Lab scene lacks a mesh instance");
        }
        std::cout<<"OBJ mesh and Scene V3 integration PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
