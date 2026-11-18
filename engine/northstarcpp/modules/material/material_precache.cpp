// Material precache dependency graph: VFS-safe DAG discovery, material
// instance inheritance, nested functions and typed shader generation.
#include <northstarcpp/material_precache.hpp>
#include <northstarcpp/material.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace northstarcpp {
namespace {
using Clock=std::chrono::steady_clock;
using Tree=boost::property_tree::ptree;
bool ends(std::string const& source,std::string const& tail) {
    return source.size()>=tail.size() &&
           source.compare(source.size()-tail.size(),tail.size(),tail)==0;
}
bool virtual_path(std::string const& s) {
    if(s.empty()||s.size()>240 || s.front()=='/'||s.back()=='/'||
       s.find('\\')!=std::string::npos ||
       s.find(':')!=std::string::npos ||
       s.find('@')!=std::string::npos)return false;
    std::size_t start=0;
    while(start<s.size()) {
        auto end=s.find('/',start);
        if(end==std::string::npos)end=s.size();
        auto part=s.substr(start,end-start);
        if(part.empty()||part=="."||part==".."||part.size()>100)return false;
        start=end+1;
    }
    return true;
}
Tree read_json(std::string const& data) {
    std::istringstream stream(data);
    Tree root;
    boost::property_tree::read_json(stream,root);
    return root;
}
std::string encode(Tree const& tree) {
    std::ostringstream stream;
    boost::property_tree::write_json(stream,tree,false);
    return stream.str();
}
struct MaterialMetadata {
    Tree graph;
    bool has_graph=false;
    unsigned blend=0;
    float cutoff=0.5f;
    std::vector<std::string> parents;
};
struct Planner {
    MaterialPrecacheReader reader;
    MaterialPrecachePlan plan;
    std::map<std::string,std::string> file_cache;
    std::set<std::string> functions_in_progress;
    std::set<std::string> functions_done;
    std::set<std::string> materials_in_progress;
    explicit Planner(MaterialPrecacheReader loader):reader(std::move(loader)) {
        if(!reader)throw std::invalid_argument("Material precache VFS reader missing");
    }
    std::string const& load(std::string const& path,std::size_t limit) {
        if(!virtual_path(path))
            throw std::invalid_argument("Unsafe Material Precache VFS path: "+path);
        auto found=file_cache.find(path);
        if(found!=file_cache.end())return found->second;
        if(file_cache.size()>=256)
            throw std::invalid_argument("Material Precache dependency file limit");
        std::string bytes=reader(path);
        if(bytes.empty()||bytes.size()>limit || plan.bytes_read>4*1024*1024-bytes.size())
            throw std::invalid_argument("Material Precache source exceeds byte budget: "+path);
        plan.bytes_read+=bytes.size();
        return file_cache.emplace(path,std::move(bytes)).first->second;
    }
    void visit_function(std::string const& path,unsigned depth) {
        if(!ends(path,".material-function.json"))
            throw std::invalid_argument("Expected Material Function resource: "+path);
        if(functions_done.count(path))return;
        if(depth>8 || functions_in_progress.size()>=16)
            throw std::invalid_argument("Material Function precache recursion limit");
        if(!functions_in_progress.insert(path).second)
            throw std::invalid_argument("Material Function precache dependency cycle: "+path);
        try {
            Tree const json=read_json(load(path,32768));
            if(json.get<std::string>("schema")!="northstar.material-function.v1")
                throw std::invalid_argument("Unsupported Material Function version: "+path);
            auto const& nodes=json.get_child("nodes");
            if(nodes.size()>64)
                throw std::invalid_argument("Material Function node limit exceeded");
            for(auto const& [tag,node]:nodes) {
                if(!tag.empty())
                    throw std::invalid_argument("Material Function nodes are not an array");
                if(node.get<std::string>("op","")=="function")
                    visit_function(node.get<std::string>("path"),depth+1);
            }
            functions_in_progress.erase(path);
            functions_done.insert(path);
            plan.function_order.push_back(path); // topological post-order
        }catch(...) {
            functions_in_progress.erase(path);
            throw;
        }
    }
    MaterialMetadata material(std::string const& path,unsigned depth=0) {
        if(depth>8 || !materials_in_progress.insert(path).second)
            throw std::invalid_argument("Material precache inheritance cycle: "+path);
        try {
            bool instance=ends(path,".material-instance.json");
            bool native=ends(path,".ymat");
            if(!instance&&!native)
                throw std::invalid_argument("Material precache requires native XML YMAT");
            if(native) {
                auto const& binary=load(path,512*1024);
                auto const list=shared_assets::load_materials_from_bytes(
                    std::vector<std::uint8_t>(binary.begin(),binary.end()),path);
                if(list.empty())
                    throw std::invalid_argument("Native YMAT has no materials");
                MaterialMetadata result;
                auto const& material=list.front();
                result.blend=material.blend=="alpha" ||
                             material.blend=="translucent"?2:
                             material.blend=="masked" || material.alpha_cutoff>0?1:0;
                result.cutoff=material.alpha_cutoff;
                if(!material.graph_source.empty()) {
                    result.graph=read_json(material.graph_source);
                    result.has_graph=true;
                }
                materials_in_progress.erase(path);
                return result;
            }
            auto doc=read_json(load(path,512*1024));
            if(doc.get<unsigned>("schema")!=1)
                throw std::invalid_argument("Unsupported material version");
            MaterialMetadata meta;
            if(instance) {
                auto parent=doc.get<std::string>("parent");
                if(parent==path||!virtual_path(parent))
                    throw std::invalid_argument("Unsafe Material Precache parent");
                meta=material(parent,depth+1);
                meta.parents.push_back(parent);
            }
            Tree const* source=&doc;
            if(instance) {
                auto overrides=doc.get_child_optional("overrides");
                source=overrides?&*overrides:nullptr;
            }
            if(source) {
                if(auto graph=source->get_child_optional("graph")) {
                    meta.graph=*graph;
                    meta.has_graph=true;
                }
                if(auto blend=source->get_optional<std::string>("blend_mode")) {
                    if(*blend=="opaque")meta.blend=0;
                    else if(*blend=="masked")meta.blend=1;
                    else if(*blend=="translucent")meta.blend=2;
                    else throw std::invalid_argument("Unknown material blend mode");
                }
                if(auto cutoff=source->get_optional<float>("alpha_cutoff")) {
                    if(!std::isfinite(*cutoff)||*cutoff<0||*cutoff>1)
                        throw std::invalid_argument("Material cutoff invalid");
                    meta.cutoff=*cutoff;
                }
            }
            materials_in_progress.erase(path);
            return meta;
        }catch(...) {
            materials_in_progress.erase(path);
            throw;
        }
    }
    void one(std::string const& path) {
        MaterialMetadata meta=material(path);
        MaterialPrecacheVariant variant;
        variant.material_reference=path;
        variant.blend_mode=meta.blend;
        variant.alpha_cutoff=meta.cutoff;
        variant.dependencies=meta.parents;
        if(meta.has_graph) {
            auto const& nodes=meta.graph.get_child("nodes");
            for(auto const& [tag,node]:nodes) {
                if(!tag.empty())
                    throw std::invalid_argument("Material Graph nodes not an array");
                if(node.get<std::string>("op","")=="function")
                    visit_function(node.get<std::string>("path"),1);
            }
            // The dependency DAG has already been indexed, so the compiler
            // reuses bytes instead of rereading function sources from VFS.
            auto const begin=Clock::now();
            variant.graph_shader_defines=compile_material_graph_with_functions(
                encode(meta.graph),[&](std::string const& function_path) {
                    if(!ends(function_path,".material-function.json"))
                        throw std::invalid_argument("Material precache function ref invalid");
                    return load(function_path,32768);
                });
            variant.graph_compile_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-begin).count();
            std::set<std::string> reachable;
            std::function<void(std::string const&)> collect=
                [&](std::string const& ref) {
                    if(!reachable.insert(ref).second)return;
                    Tree const json=read_json(load(ref,32768));
                    for(auto const& [tag,node]:json.get_child("nodes"))
                        if(node.get<std::string>("op","")=="function")
                            collect(node.get<std::string>("path"));
                };
            for(auto const& [tag,node]:nodes)
                if(node.get<std::string>("op","")=="function")
                    collect(node.get<std::string>("path"));
            for(auto const& dependency:plan.function_order)
                if(reachable.count(dependency))
                    variant.dependencies.push_back(dependency);
        }
        plan.variants.push_back(std::move(variant));
    }
};
}
MaterialPrecachePlan build_material_precache_plan(
    MaterialPrecacheReader const& reader,
    std::vector<std::string> const& materials) {
    if(materials.size()>128)
        throw std::invalid_argument("Material precache supports <=128 materials");
    auto const start=Clock::now();
    Planner planner(reader);
    std::set<std::string> used;
    for(auto const& path:materials)
        if(used.insert(path).second)
            planner.one(path);
    planner.plan.planning_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-start).count();
    return std::move(planner.plan);
}
MaterialPrecachePlan build_material_precache_plan(
    AssetStore const& assets,std::vector<std::string> const& materials) {
    return build_material_precache_plan(
        [&assets](std::string const& reference) {
            if(assets.size(reference)>512*1024)
                throw std::invalid_argument("Material precache asset too large");
            auto bytes=assets.read(reference);
            return std::string(bytes.begin(),bytes.end());
        },materials);
}
} // namespace northstarcpp
