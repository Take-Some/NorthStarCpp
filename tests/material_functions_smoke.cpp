// Material Functions: input/output typing, multiple outputs, nested linking,
// missing function, invalid types, cycles and safe resource limits.
#include <northstarcpp/material.hpp>
#include <northstarcpp/assets.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;

static void check(bool valid,char const* reason) {
    if(!valid)throw std::runtime_error(reason);
}
static void rejects(std::string const& graph,
    northstarcpp::MaterialFunctionResolver const& resolver,
    std::string const& expected) {
    try {(void)northstarcpp::compile_material_graph_with_functions(graph,resolver);}
    catch(std::exception const& e) {
        if(std::string(e.what()).find(expected)!=std::string::npos)return;
        throw std::runtime_error("Wrong diagnostic: "+std::string(e.what()));
    }
    throw std::runtime_error("Invalid Material Function unexpectedly accepted");
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        fs::path root=fs::u8path(argv[1]);
        northstarcpp::MaterialFunctionResolver file=[&](std::string const& ref) {
            auto p=root/fs::u8path(ref);
            if(!fs::is_regular_file(p))throw std::runtime_error("Function not found "+ref);
            if(fs::file_size(p)>32768)throw std::runtime_error("Oversized function");
            std::ifstream stream(p,std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(stream),{});
        };
        std::string graph=R"({
          "nodes":[
            {"id":"c","op":"parameter","parameter":"base_color"},
            {"id":"strength","op":"scalar","value":0.42},
            {"id":"out","op":"function",
              "path":"MaterialFunctions/SoftTint.material-function.json",
              "output":"result",
              "inputs":{"color":"c","mix":"strength"}}
          ],"outputs":{"base_color":"out"}})";
        auto lowered=northstarcpp::expand_material_graph_functions(graph,file);
        check(lowered.find("function_input")==std::string::npos &&
              lowered.find("\"op\":\"function\"")==std::string::npos,
              "Compiled graph must not expose function operations");
        auto hlsl=northstarcpp::compile_material_graph_with_functions(graph,file);
        check(hlsl.find("NS_GRAPH_BASE")!=std::string::npos &&
              hlsl.find("lerp(")!=std::string::npos,
              "Nested Material Function did not compile");
        northstarcpp::AssetStore assets;
        assets.mount_directory("",root);
        auto runtime=northstarcpp::load_material_definition(
            assets,"Materials/SoftTintDemo.material.json");
        check(runtime.graph_shader_defines.find("lerp(")!=std::string::npos,
              "Actual AssetStore VFS path failed nested Material Function loading");
        auto multi=R"({
          "nodes":[
            {"id":"c","op":"color","value":[0.2,0.4,0.6,0.8]},
            {"id":"t","op":"color","value":[1.0,0.8,0.6,1.0]},
            {"id":"alpha","op":"function",
              "path":"MaterialFunctions/MultiplyColor.material-function.json",
              "output":"alpha","inputs":{"color":"c","tint":"t"}}
          ],"outputs":{"opacity":"alpha"}})";
        check(northstarcpp::compile_material_graph_with_functions(multi,file)
              .find("NS_GRAPH_OPACITY")!=std::string::npos,
              "Function multi-output selection failed");
        auto wrong=R"({
          "nodes":[
            {"id":"c","op":"vector3","value":[0.2,0.3,0.5]},
            {"id":"a","op":"color","value":[1,1,1,1]},
            {"id":"out","op":"function",
              "path":"MaterialFunctions/MultiplyColor.material-function.json",
              "output":"result","inputs":{"color":"c","tint":"a"}}
          ],"outputs":{"base_color":"out"}})";
        rejects(wrong,file,"requires vector4");
        auto missing=R"({
          "nodes":[
            {"id":"c","op":"color","value":[0.2,0.4,0.6,1]},
            {"id":"out","op":"function",
              "path":"MaterialFunctions/MultiplyColor.material-function.json",
              "output":"result","inputs":{"color":"c"}}
          ],"outputs":{"base_color":"out"}})";
        rejects(missing,file,"input count mismatch");
        auto traversal=R"({
          "nodes":[{"id":"out","op":"function","path":"../outside.material-function.json",
            "output":"result","inputs":{}}],
          "outputs":{"base_color":"out"}})";
        rejects(traversal,file,"Unsafe Material Function VFS reference");
        auto recursive=R"({"schema":"northstar.material-function.v1",
          "inputs":[{"name":"color","type":"vector4"}],
          "outputs":[{"name":"result","type":"vector4","node":"inner"}],
          "nodes":[{"id":"base","op":"function_input","name":"color"},
            {"id":"inner","op":"function","path":"MaterialFunctions/Recursive.material-function.json",
             "output":"result","inputs":{"color":"base"}}]})";
        northstarcpp::MaterialFunctionResolver cycle=[&](std::string const& ref) {
            if(ref=="MaterialFunctions/Recursive.material-function.json")
                return std::string(recursive);
            return file(ref);
        };
        auto loop=R"({
          "nodes":[
            {"id":"c","op":"color","value":[1,1,1,1]},
            {"id":"out","op":"function",
              "path":"MaterialFunctions/Recursive.material-function.json",
              "output":"result","inputs":{"color":"c"}}
          ],"outputs":{"base_color":"out"}})";
        rejects(loop,cycle,"dependency cycle");
        std::cout<<"Material Function linker, typed IO, nested calls, multi-output and safety: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Material Function QA FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
