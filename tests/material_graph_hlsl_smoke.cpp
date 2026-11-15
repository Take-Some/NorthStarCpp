// Compile typed Material Graph HLSL through the Windows shader toolchain.
#include <northstarcpp/material.hpp>
#include "material_hlsl.hpp"
#include <d3dcompiler.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void compile(std::string const& source,char const* entry,char const* profile) {
    ID3DBlob* shader=nullptr;
    ID3DBlob* errors=nullptr;
    auto hr=D3DCompile(source.data(),source.size(),"typed-material",
        nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,
        &shader,&errors);
    std::string error;
    if(errors) {
        error.assign(static_cast<char const*>(errors->GetBufferPointer()),
                     errors->GetBufferSize());
        errors->Release();
    }
    if(shader)shader->Release();
    if(FAILED(hr))
        throw std::runtime_error(std::string(entry)+": "+error);
}
}

int main(int argc,char** argv) {
    try {
        auto graph=northstarcpp::compile_material_graph(R"({
          "nodes":[
             {"id":"uv","op":"parameter","parameter":"uv"},
             {"id":"unit","op":"vector2","value":[0.1,0.7]},
             {"id":"sum","op":"add","a":"uv","b":"unit"},
             {"id":"rgba","op":"cast","a":"sum","to":"vector4"},
             {"id":"v","op":"vector3","value":[0.6,0.2,0.8]},
             {"id":"n","op":"normalize","a":"v"},
             {"id":"dot","op":"dot","a":"n","b":"v"},
             {"id":"channel","op":"component","a":"v","component":"y"}
          ],
          "outputs":{
             "base_color":"rgba",
             "roughness":"dot",
             "metallic":"channel",
             "emissive":"n"
          }})");
        std::string shader=graph+northstarcpp::shaders::authored_material_shader;
        compile(shader,"material_vs","vs_5_0");
        compile(shader,"material_ps","ps_5_0");
        compile(shader,"material_gbuffer_ps","ps_5_0");
        if(argc==2) {
            auto root=std::filesystem::u8path(argv[1]);
            northstarcpp::MaterialFunctionResolver resolver=[&](std::string const& ref) {
                auto path=root/std::filesystem::u8path(ref);
                if(!std::filesystem::is_regular_file(path) ||
                   std::filesystem::file_size(path)>32768)
                    throw std::runtime_error("Material Function HLSL test cannot read "+ref);
                std::ifstream file(path,std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(file),{});
            };
            auto graphWithFunctions=northstarcpp::compile_material_graph_with_functions(
                R"({"nodes":[
                    {"id":"c","op":"parameter","parameter":"base_color"},
                    {"id":"mix","op":"scalar","value":0.4},
                    {"id":"f","op":"function",
                     "path":"MaterialFunctions/SoftTint.material-function.json",
                     "output":"result","inputs":{"color":"c","mix":"mix"}}
                ],"outputs":{"base_color":"f"}})",resolver);
            auto shaderWithFunctions=graphWithFunctions+
                northstarcpp::shaders::authored_material_shader;
            compile(shaderWithFunctions,"material_vs","vs_5_0");
            compile(shaderWithFunctions,"material_ps","ps_5_0");
            compile(shaderWithFunctions,"material_gbuffer_ps","ps_5_0");
        }
        std::cout<<"Typed Material Graph + nested Functions HLSL VS/PS/GBuffer: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Typed Material Graph HLSL: FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
