#include "editor_prefab_composition.hpp"
#include "editor_tools.hpp"
#include <northstarcpp/assets.hpp>
#include <cctype>
#include <filesystem>
#include <utility>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace northstarcpp::editor::prefab_preview {
namespace {
constexpr std::size_t max_parts=4096;
constexpr std::size_t max_vertices=600000;
constexpr std::size_t max_triangles=120000;
constexpr float radians=0.01745329251994329577f;

void warning(Composition& result,std::string message) {
    if(result.warnings.size()<24)
        result.warnings.push_back(std::move(message));
}
std::array<float,9> basis(SceneObject const& actor) {
    auto const x=actor.node.rotation.x*radians;
    auto const y=actor.node.rotation.y*radians;
    auto const z=actor.node.rotation.z*radians;
    auto const sx=std::sin(x),cx=std::cos(x);
    auto const sy=std::sin(y),cy=std::cos(y);
    auto const sz=std::sin(z),cz=std::cos(z);
    return {cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx,
            sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx,
            -sy,cy*sx,cy*cx};
}
std::array<float,3> transform_basis(std::array<float,9> const& b,
                           std::array<float,3> const& v) {
    return {b[0]*v[0]+b[1]*v[1]+b[2]*v[2],
            b[3]*v[0]+b[4]*v[1]+b[5]*v[2],
            b[6]*v[0]+b[7]*v[1]+b[8]*v[2]};
}
std::array<float,3> normalized(std::array<float,3> n) {
    auto const norm=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
    if(norm>1.e-10f)for(auto& v:n)v/=norm;
    return n;
}
shared_assets::MeshPart unit_cube() {
    shared_assets::MeshPart part;
    part.name="Unassigned scene cube";
    // The existing scene cube uses a ground anchored unit height.
    const std::array<std::array<float,3>,8> p{{
        {{-.5f,0,-.5f}},{{.5f,0,-.5f}},
        {{.5f,1,-.5f}},{{-.5f,1,-.5f}},
        {{-.5f,0,.5f}},{{.5f,0,.5f}},
        {{.5f,1,.5f}},{{-.5f,1,.5f}}
    }};
    for(auto const& vertex:p)
        part.vertices.push_back({vertex,{{0,1,0}},{{0,0}}});
    part.indices={0,2,1,0,3,2,4,5,6,4,6,7,
                  0,1,5,0,5,4,3,7,6,3,6,2,
                  0,4,7,0,7,3,1,2,6,1,6,5};
    return part;
}
MaterialDefinition tinted(std::uint32_t color) {
    MaterialDefinition m;
    m.base_color={float((color>>16)&255)/255.f,
                  float((color>>8)&255)/255.f,
                  float(color&255)/255.f,1.f};
    return m;
}
bool safe_path(std::string const& ref) {
    if(ref.empty() || ref.front()=='/' || ref.back()=='/' ||
       ref.find('\\')!=std::string::npos ||
       ref.find(':')!=std::string::npos ||
       ref.find('\0')!=std::string::npos)return false;
    std::size_t offset=0;
    while(offset<ref.size()) {
        auto const end=ref.find('/',offset);
        auto const length=(end==std::string::npos?ref.size():end)-offset;
        if(length==0 || length>255 ||
           ref.substr(offset,length)==".." ||
           ref.substr(offset,length)==".")return false;
        offset=(end==std::string::npos?ref.size():end+1);
    }
    return true;
}
bool model_reference(std::string const& ref) {
    auto const at=ref.find('@');
    auto const path=ref.substr(0,at);
    if(!safe_path(path))return false;
    if(at!=std::string::npos) {
        auto const name=ref.substr(at+1);
        if(name.empty()||name.find_first_of("/\\@:")!=std::string::npos)
            return false;
    }
    auto e=std::filesystem::u8path(path).extension().u8string();
    std::transform(e.begin(),e.end(),e.begin(),[](unsigned char c){
        return static_cast<char>(std::tolower(c));});
    return e==".ydd" || e==".ydr";
}
} // namespace

Composition compose(Scene const& prefab,DrawableLoader const& models,
                    MaterialLoader const& resolve) {
    validate_scene(prefab);
    if(prefab.version!=3 || !models || !resolve)
        throw std::invalid_argument("Prefab 3D requires a valid Scene V3 and native loaders");
    Composition result;
    result.drawable.name=prefab.name;
    std::unordered_map<std::string,std::vector<shared_assets::Drawable>> model_cache;
    std::unordered_map<std::string,MaterialDefinition> material_cache;
    std::unordered_map<std::uint64_t,std::string> material_overrides;
    for(auto const& component:prefab.components)
        if(component.entity_id &&
           component.type==editor_tools::actor_material_component_type &&
           component.version==1 && !component.payload.empty() &&
           component.payload.size()<=1024)
            material_overrides[component.entity_id]=component.payload;
    std::unordered_set<std::string> hidden_layers;
    for(auto const& layer:prefab.layers)
        if(!layer.visible)hidden_layers.insert(layer.name);
    auto cube=unit_cube();
    bool have_bounds=false;
    std::size_t total_vertices=0;
    for(auto const& actor:prefab.objects) {
        if(!actor.node.visible || hidden_layers.count(actor.node.layer))
            continue;
        ++result.visible_objects;
        auto const rotation=basis(actor);
        std::array<float,3> const scale{
            actor.node.scale.x*actor.size,
            actor.node.scale.y*actor.size,
            actor.node.scale.z*actor.size
        };
        auto const origin=std::array<float,3>{
            actor.position.x,actor.position.y,actor.position.z};
        std::vector<shared_assets::Drawable> const* drawables=nullptr;
        if(!actor.node.asset.empty() && model_reference(actor.node.asset)) {
            auto found=model_cache.find(actor.node.asset);
            if(found==model_cache.end()) {
                try {
                    auto loaded=models(actor.node.asset);
                    if(loaded.empty())
                        throw std::runtime_error("No drawable entries");
                    found=model_cache.emplace(actor.node.asset,
                                              std::move(loaded)).first;
                }catch(std::exception const& e) {
                    warning(result,"Missing model "+actor.node.asset+
                                   " for "+actor.name+": "+e.what());
                }
            }
            if(found!=model_cache.end())drawables=&found->second;
        } else if(!actor.node.asset.empty()) {
            warning(result,"Unsupported model "+actor.node.asset+
                           " for "+actor.name);
        }
        std::vector<shared_assets::MeshPart> proxy;
        if(!drawables) {
            if(!actor.node.asset.empty())++result.missing_assets;
            proxy.push_back(cube);
            ++result.proxy_objects;
        } else ++result.modeled_objects;
        auto append_part=[&](shared_assets::MeshPart const& source,
                             std::uint32_t actor_color,bool placeholder) {
            auto const faces=source.indices.size()/3;
            if(result.drawable.meshes.size()>=max_parts ||
               source.vertices.size()>max_vertices-total_vertices ||
               faces>max_triangles-result.triangles)
                throw std::length_error("Prefab preview exceeds mesh or triangle budget");
            shared_assets::MeshPart part;
            part.name=actor.name+"/"+source.name;
            auto const assigned=material_overrides.find(actor.node.id);
            auto const material_reference=assigned==material_overrides.end()?
                source.material:assigned->second;
            part.material=material_reference;
            part.indices=source.indices;
            part.material_required=source.material_required;
            part.vertices.reserve(source.vertices.size());
            for(auto const& vertex:source.vertices) {
                shared_assets::Vertex next=vertex;
                std::array<float,3> transformed{
                    vertex.position[0]*scale[0],
                    vertex.position[1]*scale[1],
                    vertex.position[2]*scale[2]};
                auto const displaced=transform_basis(rotation,transformed);
                for(unsigned axis=0;axis<3;++axis) {
                    next.position[axis]=displaced[axis]+origin[axis];
                    if(!std::isfinite(next.position[axis]))
                        throw std::invalid_argument("Nonfinite prefab world position");
                    if(!have_bounds) {
                        result.drawable.bounds_min[axis]=next.position[axis];
                        result.drawable.bounds_max[axis]=next.position[axis];
                    } else {
                        result.drawable.bounds_min[axis]=std::min(
                            result.drawable.bounds_min[axis],next.position[axis]);
                        result.drawable.bounds_max[axis]=std::max(
                            result.drawable.bounds_max[axis],next.position[axis]);
                    }
                }
                have_bounds=true;
                std::array<float,3> normal{};
                for(unsigned axis=0;axis<3;++axis)
                    normal[axis]=std::abs(scale[axis])>1.e-8f?
                        vertex.normal[axis]/scale[axis]:0.f;
                next.normal=normalized(transform_basis(rotation,normal));
                part.vertices.push_back(next);
            }
            for(auto index:part.indices)
                if(index>=part.vertices.size())
                    throw std::invalid_argument("Invalid prefab mesh triangle index");
            MaterialDefinition m=tinted(placeholder?
                (actor.node.asset.empty()?actor_color:0xdd6085u):0xffffffu);
            if(!material_reference.empty()) {
                auto found=material_cache.find(material_reference);
                if(found==material_cache.end()) {
                    try {
                        found=material_cache.emplace(
                            material_reference,resolve(material_reference)).first;
                    }catch(std::exception const& e) {
                        warning(result,"Missing material "+material_reference+
                            ": "+e.what());
                    }
                }
                if(found!=material_cache.end())m=found->second;
            }
            result.materials.push_back(std::move(m));
            result.drawable.meshes.push_back(std::move(part));
            total_vertices+=source.vertices.size();
            result.triangles+=faces;
        };
        if(drawables) {
            for(auto const& drawable:*drawables)
                for(auto const& part:drawable.meshes)
                    append_part(part,actor.color,false);
        } else for(auto const& part:proxy)
            append_part(part,actor.color,true);
    }
    if(result.drawable.meshes.empty())
        warning(result,"No visible geometry in this prefab");
    return result;
}
void mount_preview_assets(AssetStore& assets,
                          std::filesystem::path const& content_root) {
    namespace fs=std::filesystem;
    assets.mount_directory("",content_root,100);
    for(auto current=content_root.parent_path();!current.empty();) {
        std::error_code ec;
        auto const shared=current/"sharedAssets";
        if(fs::is_directory(shared,ec) && !ec) {
            ec.clear();
            bool const already_mounted=fs::equivalent(shared,content_root,ec);
            if(ec)
                throw std::runtime_error("Cannot compare prefab preview VFS roots");
            if(!already_mounted)
                assets.mount_shared_assets(shared);
            break;
        }
        auto const parent=current.parent_path();
        if(parent==current)break;
        current=parent;
    }
}

Composition load_composition(Scene const& prefab,AssetStore const& assets) {
    return compose(prefab,[&](std::string const& ref) {
        auto const at=ref.find('@');
        auto const path=ref.substr(0,at);
        if(assets.size(path)>64u*1024u*1024u)
            throw std::length_error("Native model exceeds preview size limit");
        if(at!=std::string::npos)
            return std::vector<shared_assets::Drawable>{assets.load_drawable(ref)};
        return shared_assets::load_drawables(assets,path);
    },[&](std::string const& ref) {
        return load_material_definition(assets,ref);
    });
}
} // namespace northstarcpp::editor::prefab_preview
