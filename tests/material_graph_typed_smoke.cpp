// Material Graph typed node/operator regression — no renderer or GPU required.
#include <northstarcpp/material.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool truth,char const* message) {
    if(!truth)throw std::runtime_error(message);
}
void accepted(std::string const& source,std::string const& name) {
    auto const result=northstarcpp::compile_material_graph(source);
    require(result.find("#define NS_GRAPH_")!=std::string::npos,
            "Typed compiler produced no HLSL binding");
    require(result.find("float4(")!=std::string::npos,
            "Typed compiler must preserve float4 shader ABI");
    std::cout<<"PASS "<<name<<"\n";
}
void rejected(std::string const& source,std::string const& contains,
              std::string const& name) {
    try {
        (void)northstarcpp::compile_material_graph(source);
    }catch(std::exception const& e) {
        require(std::string(e.what()).find(contains)!=std::string::npos,
                "Compiler rejected graph without node-specific diagnostic");
        std::cout<<"PASS "<<name<<"\n";
        return;
    }
    throw std::runtime_error("Typed compiler accepted an invalid graph: "+name);
}
} // namespace
int main() {
    try {
        accepted(R"({
            "nodes":[
              {"id":"v","op":"vector2","value":[0.25,0.75]},
              {"id":"s","op":"scalar","value":0.5},
              {"id":"sum","op":"add","a":"v","b":"s"},
              {"id":"promote","op":"cast","a":"sum","to":"vector4"}
            ],
            "outputs":{"base_color":"promote"}})","Vector2 + Scalar / Cast");
        accepted(R"({
            "nodes":[
              {"id":"v","op":"vector3","value":[0.2,0.4,0.8]},
              {"id":"n","op":"normalize","a":"v"},
              {"id":"d","op":"dot","a":"n","b":"v"},
              {"id":"component","op":"component","a":"v","component":"z"}
            ],
            "outputs":{"roughness":"d","metallic":"component"}})",
            "Vector3 Normalize / Dot / Component");
        accepted(R"({
            "nodes":[
              {"id":"base","op":"parameter","parameter":"uv"},
              {"id":"rgba","op":"cast","a":"base","to":"vector4"}
            ],
            "outputs":{"base_color":"rgba"}})","UV Vector2 -> Color4");
        accepted(R"({
            "nodes":[{"id":"base","op":"color","value":[1,0,0,1]}],
            "outputs":{"base_color":"base"}})","Legacy color graph");
        rejected(R"({
            "nodes":[
              {"id":"a","op":"vector2","value":[0.1,0.2]},
              {"id":"b","op":"vector3","value":[0.1,0.2,0.3]},
              {"id":"bad_add","op":"add","a":"a","b":"b"}
            ],"outputs":{"base_color":"bad_add"}})",
            "Material Graph node bad_add:","Mismatched vector widths");
        rejected(R"({
            "nodes":[
              {"id":"a","op":"vector2","value":[0.1,0.2]},
              {"id":"b","op":"vector3","value":[0.1,0.2,0.3]},
              {"id":"bad_dot","op":"dot","a":"a","b":"b"}
            ],"outputs":{"roughness":"bad_dot"}})",
            "Material Graph node bad_dot:","Mismatched Dot dimensions");
        rejected(R"({
            "nodes":[
              {"id":"a","op":"vector2","value":[0.1,0.2]},
              {"id":"bad_mask","op":"component","a":"a","component":"z"}
            ],"outputs":{"roughness":"bad_mask"}})",
            "Material Graph node bad_mask:","Component outside source width");
        rejected(R"({
            "nodes":[
              {"id":"s","op":"scalar","value":1},
              {"id":"bad_norm","op":"normalize","a":"s"}
            ],"outputs":{"emissive":"bad_norm"}})",
            "Material Graph node bad_norm:","Normalize of scalar");
        rejected(R"({
            "nodes":[
              {"id":"bad_cast","op":"cast","a":"bad_cast","to":"vector4"}
            ],"outputs":{"base_color":"bad_cast"}})",
            "directed cycle","Directed cycle");
        rejected(R"({
            "nodes":[{"id":"bad_vec","op":"vector2","value":[1,2,3]}],
            "outputs":{"base_color":"bad_vec"}})",
            "Vector2 requires exactly two","Invalid vector literal");
        std::cout<<"Material Graph typed shader ABI: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Material Graph typed shader ABI: FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
