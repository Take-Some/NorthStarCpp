// Bounded Material Function linker. Reusable functions are expanded into the
// existing typed DAG; the renderer sees only regular graph shader expressions.
// No project-supplied HLSL is executed, and the plugin ABI is unchanged.
#include <northstarcpp/material.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace northstarcpp {
namespace {
using Tree=boost::property_tree::ptree;

bool ident(std::string const& text) {
    if(text.empty()||text.size()>48)return false;
    for(unsigned char c:text)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'))return false;
    return true;
}
bool type_name(std::string const& type) {
    return type=="scalar"||type=="vector2"||
           type=="vector3"||type=="vector4";
}
bool safe_function_ref(std::string const& path) {
    std::string const suffix=".material-function.json";
    if(path.size()<=suffix.size()||path.substr(path.size()-suffix.size())!=suffix ||
       path.front()=='/'||path.back()=='/' ||path.size()>240 ||
       path.find('\\')!=std::string::npos||
       path.find(':')!=std::string::npos||
       path.find('@')!=std::string::npos)return false;
    std::size_t start=0;
    while(start<path.size()) {
        auto end=path.find('/',start);
        if(end==std::string::npos)end=path.size();
        auto const part=path.substr(start,end-start);
        if(part.empty()||part=="."||part==".."||part.size()>100)return false;
        start=end+1;
    }
    return true;
}
Tree json(std::string const& source,std::size_t limit) {
    if(source.empty()||source.size()>limit)
        throw std::invalid_argument("Material Function JSON exceeds size budget");
    Tree tree;
    std::istringstream data(source);
    boost::property_tree::read_json(data,tree);
    return tree;
}
std::string serialize(Tree const& tree) {
    std::ostringstream out;
    boost::property_tree::write_json(out,tree,false);
    return out.str();
}
struct Signature {
    std::string name,type,node;
};
std::vector<Signature> signature(Tree const& root,std::string const& key) {
    auto const& elements=root.get_child(key);
    if(elements.size()>8 || (key=="outputs" && elements.empty()))
        throw std::invalid_argument("Material Function "+key+
            " requires up to 8 pins (at least one output)");
    std::set<std::string> names;
    std::vector<Signature> result;
    for(auto const& [tag,element]:elements) {
        if(!tag.empty())
            throw std::invalid_argument("Material Function pin list must be an array");
        Signature pin{element.get<std::string>("name"),
                      element.get<std::string>("type"),
                      element.get<std::string>("node","")};
        if(!ident(pin.name)||!type_name(pin.type)||
           !names.insert(pin.name).second||
           (key=="outputs"&&!ident(pin.node)))
            throw std::invalid_argument("Invalid Material Function "+key+" signature");
        result.push_back(std::move(pin));
    }
    return result;
}
struct Expander {
    MaterialFunctionResolver const& resolve;
    std::map<std::string,Tree> cache;
    std::set<std::string> loading,used;
    Tree flattened;
    unsigned serial=0;
    unsigned total_nodes=0;
    explicit Expander(MaterialFunctionResolver const& loader):resolve(loader) {}

    std::string fresh() {
        std::string id;
        do {id="mf_"+std::to_string(++serial);} while(used.count(id));
        used.insert(id);
        return id;
    }
    void append(Tree node) {
        if(++total_nodes>64)
            throw std::invalid_argument(
                "Material Function graph exceeds 64 expanded nodes");
        flattened.push_back({"",std::move(node)});
    }
    Tree const& load(std::string const& ref) {
        if(!safe_function_ref(ref))
            throw std::invalid_argument("Unsafe Material Function VFS reference");
        auto found=cache.find(ref);
        if(found!=cache.end())return found->second;
        if(cache.size()>=16)
            throw std::invalid_argument("Material Function dependency limit exceeded");
        if(!resolve)
            throw std::invalid_argument("Material Function VFS resolver unavailable");
        auto text=resolve(ref);
        auto tree=json(text,32768);
        if(tree.get<std::string>("schema")!="northstar.material-function.v1")
            throw std::invalid_argument("Unsupported Material Function schema: "+ref);
        return cache.emplace(ref,std::move(tree)).first->second;
    }
    static std::map<std::string,std::string> refs(Tree const& node) {
        std::map<std::string,std::string> result;
        for(auto const& [name,pin]:node.get_child("inputs")) {
            if(!ident(name)||!ident(pin.get_value<std::string>()) ||
               !result.emplace(name,pin.get_value<std::string>()).second)
                throw std::invalid_argument("Invalid Material Function call input");
        }
        return result;
    }
    static void replace_inputs(Tree& node,
        std::map<std::string,std::string> const& mapping) {
        for(auto const* key:{"a","b","t"}) {
            auto input=node.get_optional<std::string>(key);
            if(!input||input->empty())continue;
            auto found=mapping.find(*input);
            if(found==mapping.end())
                throw std::invalid_argument(
                    "Function node references unknown internal input "+*input);
            node.put(key,found->second);
        }
        if(node.get<std::string>("op")=="function") {
            auto& pins=node.get_child("inputs");
            for(auto& [name,pin]:pins) {
                auto const old=pin.get_value<std::string>();
                auto found=mapping.find(old);
                if(found==mapping.end())
                    throw std::invalid_argument(
                        "Nested function references missing input "+old);
                pin.put_value(found->second);
            }
        }
    }
    void call(Tree const& caller,std::string const& call_id,unsigned depth) {
        auto const path=caller.get<std::string>("path");
        if(depth>4)
            throw std::invalid_argument("Material Function recursion depth exceeded");
        if(loading.count(path))
            throw std::invalid_argument("Material Function dependency cycle: "+path);
        // Copy: std::map insertions by nested calls cannot invalidate this tree.
        Tree const function=load(path);
        loading.insert(path);
        try {
            auto const inputs=signature(function,"inputs");
            auto const outputs=signature(function,"outputs");
            auto const bound=refs(caller);
            if(bound.size()!=inputs.size())
                throw std::invalid_argument("Material Function input count mismatch");
            auto const selected=caller.get<std::string>("output");
            auto result=std::find_if(outputs.begin(),outputs.end(),
                [&](Signature const& pin){return pin.name==selected;});
            if(result==outputs.end())
                throw std::invalid_argument("Material Function output not found: "+selected);

            std::set<std::string> input_names;
            for(auto const& pin:inputs) {
                input_names.insert(pin.name);
                if(bound.find(pin.name)==bound.end())
                    throw std::invalid_argument("Missing Material Function input "+pin.name);
            }
            auto const& raw=function.get_child("nodes");
            if(raw.empty()||raw.size()>64)
                throw std::invalid_argument("Material Function requires 1..64 nodes");

            std::map<std::string,std::string> names;
            std::map<std::string,std::string> input_nodes;
            for(auto const& [tag,node]:raw) {
                if(!tag.empty())throw std::invalid_argument("Material Function nodes must be an array");
                auto id=node.get<std::string>("id");
                if(!ident(id)||!names.emplace(id,fresh()).second)
                    throw std::invalid_argument("Duplicate or invalid Material Function node");
                if(node.get<std::string>("op")=="function_input") {
                    auto name=node.get<std::string>("name");
                    if(!input_names.count(name)||!input_nodes.emplace(name,id).second)
                        throw std::invalid_argument("Invalid Material Function input node");
                }
            }
            if(input_nodes.size()!=inputs.size())
                throw std::invalid_argument("Material Function input nodes do not match signature");
            for(auto const& pin:inputs) {
                Tree check;
                check.put("id",names.at(input_nodes.at(pin.name)));
                check.put("op","type_assert");
                check.put("a",bound.at(pin.name));
                check.put("to",pin.type);
                append(std::move(check));
            }
            for(auto const& [tag,original]:raw) {
                if(original.get<std::string>("op")=="function_input")continue;
                Tree node=original;
                node.put("id",names.at(original.get<std::string>("id")));
                replace_inputs(node,names);
                if(node.get<std::string>("op")=="function")
                    call(node,node.get<std::string>("id"),depth+1);
                else append(std::move(node));
            }
            auto const target=names.find(result->node);
            if(target==names.end())
                throw std::invalid_argument("Material Function output references absent node");
            Tree output;
            output.put("id",call_id);
            output.put("op","type_assert");
            output.put("to",result->type);
            output.put("a",target->second);
            append(std::move(output));
        }catch(std::exception const& e) {
            loading.erase(path);
            throw std::invalid_argument("Material Function "+path+
                " called at node "+call_id+": "+e.what());
        }
        loading.erase(path);
    }
    std::string run(std::string const& source) {
        Tree root=json(source,32768);
        auto const& nodes=root.get_child("nodes");
        if(nodes.empty()||nodes.size()>64)
            throw std::invalid_argument("Material Graph requires 1..64 nodes");
        for(auto const& [tag,node]:nodes) {
            if(!tag.empty())
                throw std::invalid_argument("Material Graph nodes must be an array");
            auto id=node.get<std::string>("id");
            if(!ident(id)||!used.insert(id).second)
                throw std::invalid_argument("Duplicate or invalid Material Graph node");
        }
        for(auto const& [tag,node]:nodes) {
            if(node.get<std::string>("op")=="function")
                call(node,node.get<std::string>("id"),1);
            else append(node);
        }
        root.put_child("nodes",flattened);
        root.erase("groups"); // editor-only metadata, not GPU shader source
        auto result=serialize(root);
        if(result.size()>32768)
            throw std::invalid_argument("Expanded Material Graph exceeds 32 KiB");
        return result;
    }
};
}
std::string expand_material_graph_functions(
    std::string const& graph_json,MaterialFunctionResolver const& resolver) {
    if(graph_json.empty())return {};
    return Expander{resolver}.run(graph_json);
}
std::string compile_material_graph_with_functions(
    std::string const& graph_json,MaterialFunctionResolver const& resolver) {
    if(graph_json.empty())return {};
    auto expanded=expand_material_graph_functions(graph_json,resolver);
    return compile_material_graph(expanded);
}
} // namespace northstarcpp
