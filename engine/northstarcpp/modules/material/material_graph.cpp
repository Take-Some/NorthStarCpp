// NorthStar Material Graph compiler — bounded, typed DAG → safe HLSL expressions.
// No direct HLSL from project files is accepted.
#include <northstarcpp/material.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace northstarcpp {
namespace {
using Tree=boost::property_tree::ptree;
struct Node {
    std::string id,op,a,b,t,slot,parameter,component,to;
    std::array<float,4> value{0,0,0,1};
};
bool identifier(std::string const& text) {
    if(text.empty()||text.size()>48)return false;
    for(unsigned char c:text)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'))return false;
    return true;
}
std::string num(float x) {
    if(!std::isfinite(x)||std::abs(x)>64)
        throw std::invalid_argument("Graph scalar must be finite in [-64,64]");
    std::ostringstream out;
    out<<std::setprecision(8)<<std::fixed<<x;
    return out.str();
}
std::string value4(std::array<float,4> const& v) {
    return "float4("+num(v[0])+","+num(v[1])+","+num(v[2])+","+num(v[3])+")";
}
std::array<float,4> parse_value(Tree const& field) {
    std::array<float,4> out{0,0,0,1};
    if(field.empty()) {
        out.fill(field.get_value<float>());
    }else {
        if(field.size()<2||field.size()>4)
            throw std::invalid_argument("Graph vector must contain 2..4 numbers");
        std::size_t n=0;
        for(auto const& [key,val]:field) {
            if(!key.empty())throw std::invalid_argument("Graph vector must be an array");
            out[n++]=val.get_value<float>();
        }
    }
    for(float c:out)(void)num(c);
    return out;
}
std::string texture(std::string const& slot) {
    static constexpr std::array<char const*,5> names{
        "base_color","normal","roughness","metallic","emissive"};
    static constexpr std::array<char const*,5> samplers{
        "base_map","normal_map","rough_map","metal_map","emissive_map"};
    for(unsigned i=0;i<5;++i)if(slot==names[i]) {
        return "(((present.x & "+std::to_string(1u<<i)+"u)!=0u) ? "
            +std::string(samplers[i])+".Sample(material_sampler,(p).uv)"
            +" : float4(1,1,1,1))";
    }
    throw std::invalid_argument("Unsupported graph texture slot");
}
std::string parameter(std::string const& name) {
    if(name=="base_color")return "base_color";
    if(name=="roughness")return "float4(factors.x,factors.x,factors.x,factors.x)";
    if(name=="metallic")return "float4(factors.y,factors.y,factors.y,factors.y)";
    if(name=="opacity")return "float4(base_color.a,base_color.a,base_color.a,base_color.a)";
    if(name=="uv")return "float4((p).uv,0,1)";
    throw std::invalid_argument("Unsupported graph material parameter");
}
struct Builder {
    std::map<std::string,Node> nodes;
    std::unordered_map<std::string,std::string> memo;
    std::unordered_set<std::string> stack;
    std::unordered_set<std::string> width_stack;
    std::unordered_map<std::string,unsigned> type_widths;
    unsigned expanded=0;
    unsigned width(std::string const& id,unsigned depth=0) {
        if(depth>32)throw std::invalid_argument("Graph type depth exceeded");
        if(auto it=type_widths.find(id);it!=type_widths.end())return it->second;
        auto it=nodes.find(id);
        if(it==nodes.end())throw std::invalid_argument("Graph references unknown node "+id);
        if(!width_stack.insert(id).second)
            throw std::invalid_argument("Material Graph node "+id+": directed cycle");
        auto const& n=it->second;
        unsigned result=4;
        auto child=[&](std::string const& ref) {
            if(ref.empty())throw std::invalid_argument("Missing connected input");
            return width(ref,depth+1);
        };
        auto combined=[&](unsigned a,unsigned b) -> unsigned {
            if(a!=b&&a!=1&&b!=1)
                throw std::invalid_argument(
                    "Incompatible vector widths; insert an explicit Cast");
            return std::max(a,b);
        };
        try {
            if(n.op=="scalar")result=1;
            else if(n.op=="vector2")result=2;
            else if(n.op=="vector3")result=3;
            else if(n.op=="color"||n.op=="texture")result=4;
            else if(n.op=="parameter") {
                if(n.parameter=="uv")result=2;
                else if(n.parameter=="roughness"||n.parameter=="metallic"||
                        n.parameter=="opacity")result=1;
                else if(n.parameter=="base_color")result=4;
                else throw std::invalid_argument("Unsupported material parameter");
            }else if(n.op=="add"||n.op=="multiply") {
                result=combined(child(n.a),child(n.b));
            }else if(n.op=="lerp") {
                result=combined(child(n.a),child(n.b));
                unsigned t=child(n.t);
                if(t!=1 && t!=result)
                    throw std::invalid_argument("Lerp factor has incompatible width");
            }else if(n.op=="one_minus"||n.op=="saturate") {
                result=child(n.a);
            }else if(n.op=="normalize") {
                result=child(n.a);
                if(result<2)throw std::invalid_argument(
                    "Normalize requires vector2, vector3 or vector4");
            }else if(n.op=="dot") {
                auto a=child(n.a),b=child(n.b);
                if(a!=b || a<2)
                    throw std::invalid_argument("Dot requires equal vector widths >=2");
                result=1;
            }else if(n.op=="component") {
                auto width=child(n.a);
                auto index=std::string("xyzw").find(n.component);
                if(n.component.size()!=1||index==std::string::npos||index>=width)
                    throw std::invalid_argument("Component exceeds input vector width");
                result=1;
            }else if(n.op=="type_assert") {
                result=child(n.a);
                unsigned target=0;
                if(n.to=="scalar")target=1;
                else if(n.to=="vector2")target=2;
                else if(n.to=="vector3")target=3;
                else if(n.to=="vector4")target=4;
                if(target==0||result!=target)
                    throw std::invalid_argument(
                        "Material Function pin requires "+n.to+
                        " but received vector width "+std::to_string(result));
            }else if(n.op=="cast") {
                (void)child(n.a);
                if(n.to=="scalar")result=1;
                else if(n.to=="vector2")result=2;
                else if(n.to=="vector3")result=3;
                else if(n.to=="vector4")result=4;
                else throw std::invalid_argument("Cast target must be scalar/vector2/3/4");
            }else throw std::invalid_argument("Unsupported typed Material Graph op");
        }catch(std::exception const& error) {
            width_stack.erase(id);
            throw std::invalid_argument("Material Graph node "+id+": "+error.what());
        }
        width_stack.erase(id);
        type_widths.emplace(id,result);
        return result;
    }
    std::string emit(std::string const& id,unsigned depth=0) {
        if(depth>32 || ++expanded>256)
            throw std::invalid_argument("Graph complexity exceeded");
        if(auto it=memo.find(id);it!=memo.end())return it->second;
        auto it=nodes.find(id);
        if(it==nodes.end())throw std::invalid_argument("Graph references unknown node "+id);
        if(!stack.insert(id).second)
            throw std::invalid_argument("Graph contains a directed cycle");
        Node const& node=it->second;
        auto child=[&](std::string const& s)->std::string {
            if(s.empty())throw std::invalid_argument("Graph node missing connected input");
            return emit(s,depth+1);
        };
        std::string expression;
        try {
        if(node.op=="scalar"||node.op=="color")expression=value4(node.value);
        else if(node.op=="vector2")
            expression="float4("+num(node.value[0])+","+num(node.value[1])+
                       ",0.0,1.0)";
        else if(node.op=="vector3")
            expression="float4("+num(node.value[0])+","+num(node.value[1])+
                       ","+num(node.value[2])+",1.0)";
        else if(node.op=="parameter")expression=parameter(node.parameter);
        else if(node.op=="texture")expression=texture(node.slot);
        else if(node.op=="add"||node.op=="multiply") {
            auto a=child(node.a),b=child(node.b);
            expression="("+a+(node.op=="add"?" + ":" * ")+b+")";
        }else if(node.op=="lerp") {
            auto a=child(node.a),b=child(node.b),t=child(node.t);
            expression="lerp("+a+","+b+",saturate("+t+"))";
        }else if(node.op=="one_minus"||node.op=="saturate") {
            auto a=child(node.a);
            expression=node.op=="one_minus"?"(1.0 - "+a+")":"saturate("+a+")";
        }else if(node.op=="normalize") {
            auto a=child(node.a);
            unsigned const dim=width(node.a);
            std::string components=dim==2?".xy":dim==3?".xyz":".xyzw";
            auto inner="normalize(("+a+")"+components+")";
            expression=dim==2?"float4("+inner+",0.0,1.0)":
                       dim==3?"float4("+inner+",1.0)":"float4("+inner+")";
        }else if(node.op=="dot") {
            auto a=child(node.a),b=child(node.b);
            unsigned const dim=width(node.a);
            auto channels=dim==2?".xy":dim==3?".xyz":".xyzw";
            auto scalar="dot(("+a+")"+channels+",("+b+")"+channels+")";
            expression="float4("+scalar+","+scalar+","+scalar+","+scalar+")";
        }else if(node.op=="component") {
            auto a=child(node.a);
            auto scalar="(("+a+")."+node.component+")";
            expression="float4("+scalar+","+scalar+","+scalar+","+scalar+")";
        }else if(node.op=="type_assert") {
            expression=child(node.a);
        }else if(node.op=="cast") {
            auto a=child(node.a);
            unsigned const old=width(node.a);
            unsigned const new_width=width(id);
            if(new_width==old)expression=a;
            else if(new_width==1) {
                auto scalar="(("+a+").x)";
                expression="float4("+scalar+","+scalar+","+scalar+","+scalar+")";
            }else if(old==1) {
                auto scalar="(("+a+").x)";
                expression="float4("+scalar+","+scalar+","+scalar+","+scalar+")";
            }else if(new_width==2)
                expression="float4(("+a+").xy,0.0,1.0)";
            else if(new_width==3) {
                expression=old==2?"float4(("+a+").xy,0.0,1.0)":
                            "float4(("+a+").xyz,1.0)";
            }else expression=old==2?"float4(("+a+").xy,0.0,1.0)":
                             old==3?"float4(("+a+").xyz,1.0)":a;
        }else throw std::invalid_argument("Unsupported material graph operation");
        if(expression.size()>8192)
            throw std::invalid_argument("Generated graph expression too long");
        }catch(std::exception const& e) {
            stack.erase(id);
            throw std::invalid_argument("Material Graph node "+id+": "+e.what());
        }
        stack.erase(id);
        return memo.emplace(id,std::move(expression)).first->second;
    }
};
}
std::string compile_material_graph(std::string const& json) {
    if(json.empty())return {};
    if(json.size()>32768)
        throw std::invalid_argument("Material Graph JSON exceeds 32 KiB");
    Tree root;
    std::istringstream input(json);
    boost::property_tree::read_json(input,root);
    auto const& raw=root.get_child("nodes");
    if(raw.empty()||raw.size()>64)
        throw std::invalid_argument("Material Graph requires 1..64 nodes");
    Builder compiler;
    for(auto const& [name,desc]:raw) {
        if(!name.empty())throw std::invalid_argument("Material graph nodes must be a JSON array");
        Node n;
        n.id=desc.get<std::string>("id");
        n.op=desc.get<std::string>("op");
        if(!identifier(n.id)||!identifier(n.op))
            throw std::invalid_argument("Invalid graph node ID or operation");
        n.a=desc.get<std::string>("a","");
        n.b=desc.get<std::string>("b","");
        n.t=desc.get<std::string>("t","");
        n.slot=desc.get<std::string>("slot","");
        n.parameter=desc.get<std::string>("parameter","");
        n.component=desc.get<std::string>("component","");
        n.to=desc.get<std::string>("to","");
        if(auto v=desc.get_child_optional("value")) {
            if(n.op=="vector2" && v->size()!=2)
                throw std::invalid_argument("Vector2 requires exactly two numbers");
            if(n.op=="vector3" && v->size()!=3)
                throw std::invalid_argument("Vector3 requires exactly three numbers");
            n.value=parse_value(*v);
        }else if(n.op=="vector2"||n.op=="vector3")
            throw std::invalid_argument("Typed vector has no value");
        if(!compiler.nodes.emplace(n.id,std::move(n)).second)
            throw std::invalid_argument("Duplicate Material Graph node ID");
    }
    auto const& outputs=root.get_child("outputs");
    if(outputs.empty()||outputs.size()>5)
        throw std::invalid_argument("Material Graph must have 1..5 output bindings");
    static constexpr std::array<char const*,5> output_names{
        "base_color","roughness","metallic","emissive","opacity"};
    static constexpr std::array<char const*,5> macros{
        "NS_GRAPH_BASE","NS_GRAPH_ROUGH","NS_GRAPH_METAL",
        "NS_GRAPH_EMISSIVE","NS_GRAPH_OPACITY"};
    std::string result;
    for(auto const& [name,reference]:outputs) {
        auto it=std::find_if(output_names.begin(),output_names.end(),
            [&](char const* match){return name==match;});
        if(it==output_names.end())
            throw std::invalid_argument("Unsupported Material Graph output: "+name);
        compiler.expanded=0;
        auto const output=reference.get_value<std::string>();
        (void)compiler.width(output);
        auto expression=compiler.emit(output);
        result+="#define "+std::string(macros[it-output_names.begin()])
            +"(p) ("+expression+")\n";
        if(result.size()>16384)
            throw std::invalid_argument("Compiled Material Graph exceeds 16 KiB");
    }
    return result;
}
} // namespace northstarcpp
