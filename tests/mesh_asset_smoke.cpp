// NorthStarCpp Wavefront OBJ / Scene V3 mesh smoke.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/mesh_asset.hpp>
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
