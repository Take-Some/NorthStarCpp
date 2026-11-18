// NorthStarCpp Wavefront OBJ / Scene V3 mesh smoke.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/native_model.hpp>
#include <northstarcpp/assets.hpp>
#include <algorithm>
#include <array>
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
        if(argc==4 && std::string(argv[1])=="--rustsea") {
            AssetStore assets;
            assets.mount_project_assets(argv[2],argv[3]);
            std::vector<std::string> models;
            for(auto const& entry:std::filesystem::directory_iterator(
                    std::filesystem::path(argv[2])/"assets/models/rustsea")) {
                if(entry.is_regular_file() && entry.path().extension()==".ydr")
                    models.push_back("models/rustsea/"+
                                     entry.path().filename().string());
            }
            std::sort(models.begin(),models.end());
            require(models.size()==45,"Rust Sea project missing native YDR assets");
            std::size_t total=0;
            try {
                auto verify_ymat=load_material_definition(assets,
                    "materials/rustsea.ymat@steel_corroded");
                std::cout<<"[rustsea] direct YMAT textures="
                         <<verify_ymat.textures[0].width<<std::endl;
            }catch(std::exception const& e) {
                std::cerr<<"[rustsea] direct YMAT decode FAILED: "<<e.what()<<std::endl;
                throw;
            }
            for(auto const& model:models) {
                auto native=load_native_model_resource(assets,model,false);
                std::cout<<"[rustsea] "<<model<<" sections="<<native.mesh.sections.size()
                         <<" loaded_materials="<<native.materials.size()<<std::endl;
                for(auto const& sec:native.mesh.sections)
                    std::cout<<"[rustsea] section YMAT="<<sec.material_reference<<std::endl;
                require(native.unresolved_materials.empty(),
                    "Rust Sea native model has unresolved YMAT slots");
                require(native.materials.size()==1,
                    "Rust Sea expected one PBR material per model");
                require(native.mesh.indices.size()>=6,
                    "Rust Sea model has missing triangles");
                auto const& material=native.materials.begin()->second;
                require(material.textures[0].width==512 &&
                        material.textures[0].height==512 &&
                        material.textures[0].rgba8.size()==512u*512u*4u,
                    "Rust Sea YTD albedo was not decoded");
                if(model.find("bastion_dust_puff.ydr")!=std::string::npos) {
                    require(material.blend_mode==2,
                        "Dust puff must have translucent native YMAT");
                    auto const& pixels=material.textures[0].rgba8;
                    require(pixels[3]==0 && pixels[(512*256+256)*4+3]>0,
                        "Dust YTD alpha channel was lost");
                }else{
                    require(material.textures[1].width==512 &&
                            material.textures[2].width==512,
                        "Rust Sea YTD normal/roughness map missing");
                }
                auto const& vertices=native.mesh.vertices;
                std::size_t nonzero=0;
                for(auto const& vertex:vertices) {
                    auto const xyz=vertex.position;
                    if(std::abs(xyz.x)+std::abs(xyz.y)+std::abs(xyz.z)>.0001f)
                        ++nonzero;
                    require(std::isfinite(xyz.x)&&std::isfinite(xyz.y)&&
                            std::isfinite(xyz.z),"Non-finite Rust Sea position");
                }
                require(nonzero*2>=vertices.size(),
                    "Rust Sea vertex stream is corrupted by auxiliary stride");
                total+=native.mesh.indices.size()/3;
            }
            std::cout<<"Rust Sea native NEF8 YDR/YMAT/YTD: PASS "
                     <<models.size()<<" assets, "<<total<<" tris with albedo/normal/roughness"
                     <<std::endl;
            return 0;
        }
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
