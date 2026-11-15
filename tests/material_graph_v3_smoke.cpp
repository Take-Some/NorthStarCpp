// Standalone material graph QA: no GPU, Qt, or provider DLL required.
// Maintains safe-whitelisted compiler contract while editor metadata expands.
#include <northstarcpp/material.hpp>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    std::string const valid=R"({
        "schema":1,
        "nodes":[
            {"id":"base","op":"parameter","parameter":"base_color","x":10,"y":15},
            {"id":"tint","op":"color","value":[0.2,0.3,0.7,1.0],"x":240,"y":15},
            {"id":"mul","op":"multiply","a":"base","b":"tint","x":420,"y":15}
        ],
        "outputs":{"base_color":"mul"},
        "groups":[{
            "id":"group_1","title":"Tint Logic","x":0,"y":-25,"w":700,"h":150,
            "members":["base","tint","mul"]
        }]
    })";
    auto compiled=northstarcpp::compile_material_graph(valid);
    if(compiled.find("#define NS_GRAPH_BASE(p) (")==std::string::npos||
       compiled.find("float4(")==std::string::npos)
        throw std::runtime_error("Comment group broke valid expression compilation");
    std::string const invalid=R"({
        "nodes":[{"id":"loop","op":"one_minus","a":"loop"}],
        "outputs":{"base_color":"loop"}})";
    bool rejected=false;
    try {
        (void)northstarcpp::compile_material_graph(invalid);
    }catch(std::invalid_argument const& error) {
        std::string message=error.what();
        rejected=message.find("Material Graph node loop:")!=std::string::npos &&
                 message.find("cycle")!=std::string::npos;
    }
    if(!rejected)
        throw std::runtime_error("Cycle rejection must include offending node");
    std::cout<<"Material Graph V3 standalone compiler regression: PASS\n";
}
