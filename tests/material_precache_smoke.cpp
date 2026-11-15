// Material Precache acceptance: VFS DAG, nested Material Functions, typed
// shader variants, duplicate suppression, inheritance and invalidation.
#include <northstarcpp/material_precache.hpp>
#include <northstarcpp/material.hpp>
#include <northstarcpp/assets.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
static void require(bool good,char const* reason) {
    if(!good)throw std::runtime_error(reason);
}
template<class Check>
static void throws(Check const& test,char const* expected) {
    try {test();}catch(std::exception const& e) {
        if(std::string(e.what()).find(expected)!=std::string::npos)return;
        throw std::runtime_error("Unexpected diagnostic: "+std::string(e.what()));
    }
    throw std::runtime_error("Invalid precache dependency graph accepted");
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        fs::path root=fs::u8path(argv[1]);
        northstarcpp::AssetStore assets;
        assets.mount_directory("",root);
        auto plan=northstarcpp::build_material_precache_plan(
            assets,{"Materials/SoftTintDemo.ymat",
                    "Materials/SoftTintDemo.ymat"});
        require(plan.variants.size()==1,"Material precache must deduplicate repeated refs");
        require(plan.function_order.size()==2 &&
            plan.function_order[0]=="MaterialFunctions/MultiplyColor.material-function.json" &&
            plan.function_order[1]=="MaterialFunctions/SoftTint.material-function.json",
            "Material Function DAG order must be dependency before caller");
        require(plan.variants[0].graph_shader_defines.find("lerp(")!=std::string::npos,
            "Precache failed to expand nested typed function graph");
        require(plan.variants[0].dependencies.size()==2,
            "Precache plan did not record transitive function references");
        auto live=northstarcpp::load_material_definition(
            assets,"Materials/SoftTintDemo.ymat");
        require(live.graph_shader_defines==plan.variants[0].graph_shader_defines,
            "Planned precache HLSL diverges from live runtime shader source");
        require(plan.bytes_read>100 && plan.planning_ms>=0 &&
                plan.variants[0].graph_compile_ms>=0,
            "Material precache instrumentation missing");
        std::map<std::string,std::string> filemap;
        auto reader=[&](std::string const& ref)->std::string {
            if(auto it=filemap.find(ref);it!=filemap.end())return it->second;
            auto path=root/fs::u8path(ref);
            if(!fs::is_regular_file(path))throw std::runtime_error("Missing fixture "+ref);
            std::ifstream stream(path,std::ios::binary);
            return {std::istreambuf_iterator<char>(stream),{}};
        };
        filemap.emplace("Materials/Inherited.material-instance.json",R"({
            "schema":1,"parent":"Materials/SoftTintDemo.ymat",
            "overrides":{"blend_mode":"masked","alpha_cutoff":0.28}})");
        auto inherited=northstarcpp::build_material_precache_plan(
            reader,{"Materials/Inherited.material-instance.json"});
        require(inherited.variants.size()==1 &&
                inherited.variants[0].blend_mode==1 &&
                inherited.variants[0].alpha_cutoff>0.27f &&
                inherited.variants[0].graph_shader_defines==live.graph_shader_defines,
                "Material instance graph inheritance or alpha state lost");
        filemap["MaterialFunctions/SoftTint.material-function.json"]=
            R"({"schema":"northstar.material-function.v1",
                "inputs":[{"name":"color","type":"vector4"},{"name":"mix","type":"scalar"}],
                "outputs":[{"name":"result","type":"vector4","node":"base"}],
                "nodes":[{"id":"base","op":"function_input","name":"color"},
                         {"id":"blend","op":"function_input","name":"mix"}]})";
        auto changed=northstarcpp::build_material_precache_plan(
            reader,{"Materials/SoftTintDemo.ymat"});
        require(changed.variants[0].graph_shader_defines!=live.graph_shader_defines,
            "Changing function source did not invalidate precache shader source");
        filemap["MaterialFunctions/SoftTint.material-function.json"]=
            R"({"schema":"northstar.material-function.v1",
                "inputs":[{"name":"color","type":"vector4"},{"name":"mix","type":"scalar"}],
                "outputs":[{"name":"result","type":"vector4","node":"loop"}],
                "nodes":[{"id":"base","op":"function_input","name":"color"},
                    {"id":"loop","op":"function",
                     "path":"MaterialFunctions/SoftTint.material-function.json",
                     "output":"result","inputs":{"color":"base","mix":"base"}}]})";
        throws([&]{(void)northstarcpp::build_material_precache_plan(
            reader,{"Materials/SoftTintDemo.ymat"});},
            "dependency cycle");
        filemap["Materials/Unsafe.material-instance.json"]=
            R"({"schema":1,"parent":"../Secret.material.json"})";
        throws([&]{(void)northstarcpp::build_material_precache_plan(
            reader,{"Materials/Unsafe.material-instance.json"});},
            "Unsafe Material Precache parent");
        std::cout<<"Material dependency precache, inheritance, mutation and cycle QA: PASS\n"
                 <<"Planning ms: "<<plan.planning_ms
                 <<" | Graph compile ms: "<<plan.variants[0].graph_compile_ms
                 <<" | Source bytes: "<<plan.bytes_read<<"\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Material precache QA: FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
