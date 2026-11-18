#pragma once
// Non-mutating visual prefab composition for Qt GPU authoring.
// A prefab supplies render parts to an existing SkeletalMesh Entity, never a Pawn.
#include <northstarcpp/player_blueprint.hpp>
#include <northstarcpp/asset_roots.hpp>
#include <northstarcpp/scene.hpp>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <map>
#include <vector>
namespace northstarcpp::editor::visual_prefab {
inline constexpr char component_type[]="northstar.scene.visual_prefab";
// Generic binding: any Scene V3 Entity, including props, NPCs and vehicles,
// may own a visual prefab without becoming a Pawn or SkeletalMesh.
inline std::string reference(Scene const& scene,std::uint64_t id) {
    for(auto const& c:scene.components)
        if(c.entity_id==id && c.type==component_type && c.version==1)
            return c.payload;
    for(auto const& c:scene.components)
        if(c.entity_id==id && c.type==player_blueprint::mesh_type &&
           c.version==player_blueprint::version) {
            auto mesh=player_blueprint::deserialize_mesh(c.payload);
            if(mesh)return mesh->prefab;
        }
    return {};
}
inline void bind(Scene& scene,std::uint64_t id,std::string ref) {
    if(scene.version!=3 || !id ||
       std::none_of(scene.objects.begin(),scene.objects.end(),
           [&](SceneObject const& o){return o.node.id==id;}))
        throw std::invalid_argument("Visual prefab requires a Scene V3 Entity");
    if(!ref.empty() &&
       (!player_blueprint::safe_asset(ref)||ref.size()<=8||
        ref.substr(ref.size()-8)!=".nprefab"))
        throw std::invalid_argument("Visual prefab must use a safe .nprefab VFS path");
    auto it=std::remove_if(scene.components.begin(),scene.components.end(),
        [&](SceneComponentBlob const& c) {
            return c.entity_id==id && c.type==component_type;
        });
    scene.components.erase(it,scene.components.end());
    if(ref.empty()) {
        // Clearing an actor's visual also clears an older Player Mesh link:
        // it must not silently reappear after removing the generic override.
        for(auto& c:scene.components)
            if(c.entity_id==id && c.type==player_blueprint::mesh_type &&
               c.version==player_blueprint::version) {
                auto mesh=player_blueprint::deserialize_mesh(c.payload);
                if(mesh && !mesh->prefab.empty()) {
                    mesh->prefab.clear();
                    c.payload=player_blueprint::serialize_mesh(*mesh);
                }
            }
    }else
        scene.components.push_back({id,component_type,1,std::move(ref)});
}
inline std::vector<std::pair<std::uint64_t,std::string>> links(Scene const& scene) {
    std::map<std::uint64_t,std::string> collected;
    // Older Player SkeletalMesh references remain supported.
    for(auto const& c:scene.components) {
        if(c.version!=player_blueprint::version ||
           c.type!=player_blueprint::mesh_type)continue;
        auto metadata=player_blueprint::deserialize_mesh(c.payload);
        if(metadata && !metadata->prefab.empty())
            collected[c.entity_id]=metadata->prefab;
    }
    // Explicit per-Entity Visual Prefab overrides the legacy mesh field.
    for(auto const& c:scene.components)
        if(c.type==component_type && c.version==1 &&
           player_blueprint::safe_asset(c.payload) &&
           c.payload.size()>8 &&
           c.payload.substr(c.payload.size()-8)==".nprefab")
            collected[c.entity_id]=c.payload;
    return {collected.begin(),collected.end()};
}
inline std::filesystem::path locate(std::filesystem::path root,std::string const& ref) {
    if(!player_blueprint::safe_asset(ref) || ref.size()<=8 ||
       ref.substr(ref.size()-8)!=".nprefab")
        throw std::invalid_argument("Invalid visual prefab reference");
    std::vector<std::filesystem::path> bases{root,root/"assets"};
    if(auto const shared=asset_paths::locate_shared_root(root);
       !shared.empty())
        bases.push_back(shared);
    for(auto const& base:bases) {
        std::error_code ec;
        auto realbase=std::filesystem::canonical(base,ec);
        if(ec)continue;
        auto real=std::filesystem::canonical(base/std::filesystem::u8path(ref),ec);
        if(ec || !std::filesystem::is_regular_file(real))continue;
        auto a=realbase.begin(),b=real.begin();
        while(a!=realbase.end() && b!=real.end() && *a==*b){++a;++b;}
        if(a!=realbase.end())continue; // disallow junction/symlink escapes
        if(std::filesystem::file_size(real)>4u*1024u*1024u)
            throw std::length_error("Visual prefab is too large");
        return real;
    }
    throw std::runtime_error("Visual prefab not found in project/sharedAssets: "+ref);
}
inline void apply(Scene& result,std::uint64_t mesh_id,Scene const& prefab) {
    validate_scene(prefab);
    if(result.version!=3 || prefab.version!=3 || prefab.objects.empty())
        throw std::invalid_argument("Visual prefab requires Scene V3");
    SceneObject const* root=nullptr;
    for(auto const& object:prefab.objects)if(!object.node.parent) {
        if(root)throw std::invalid_argument("Visual prefab needs one root");
        root=&object;
    }
    if(!root || root->node.asset.empty())
        throw std::invalid_argument("Visual prefab root has no model");
    auto target=std::find_if(result.objects.begin(),result.objects.end(),
        [&](SceneObject const& o){return o.node.id==mesh_id;});
    if(target==result.objects.end())
        throw std::invalid_argument("SkeletalMesh Entity not found");
    if(result.objects.size()+prefab.objects.size()>max_scene_objects)
        throw std::length_error("Too many prefab visual parts");
    const auto anchor=target->position;
    target->node.asset=root->node.asset;
    target->node.visible=true;
    // A generic Entity keeps authored collision, mass, scripts and controller.
    // The prefab contributes only the visible model and optional children.
    std::uint64_t next=0;
    for(auto const& o:result.objects)next=std::max(next,o.node.id);
    if(next>UINT64_MAX-prefab.objects.size())
        throw std::overflow_error("Visual Entity ID overflow");
    std::unordered_map<std::uint64_t,std::uint64_t> ids{{root->node.id,mesh_id}};
    for(auto const& o:prefab.objects)if(o.node.id!=root->node.id)
        ids.emplace(o.node.id,++next);
    for(auto const& o:prefab.objects) {
        if(o.node.id==root->node.id)continue;
        auto child=o;
        child.node.id=ids.at(o.node.id);
        child.node.parent=ids.at(o.node.parent);
        child.node.cell.clear();
        child.node.collision=false;
        child.node.script.clear();
        child.mass=0.f;
        child.position={anchor.x+o.position.x-root->position.x,
                        anchor.y+o.position.y-root->position.y,
                        anchor.z+o.position.z-root->position.z};
        result.objects.push_back(std::move(child));
    }
    for(auto c:prefab.components) {
        if(c.type!="northstar.animation.default" &&
           c.type!="engine.model.animation" &&
           c.type!="engine.material.assignment")continue;
        auto owner=ids.find(c.entity_id);
        if(owner==ids.end())continue;
        c.entity_id=owner->second;
        if(std::none_of(result.components.begin(),result.components.end(),
             [&](SceneComponentBlob const& p) {
                 return p.entity_id==c.entity_id && p.type==c.type;
             }))result.components.push_back(std::move(c));
    }
    validate_scene(result);
}
inline int authoring_index(Scene const& original,Scene const& composed,int picked) {
    if(picked<0 || std::size_t(picked)>=composed.objects.size())return -1;
    std::uint64_t id=composed.objects[std::size_t(picked)].node.id;
    // Map visual-only descendants to their nearest authoring-scene ancestor.
    for(std::size_t hops=0;hops<=composed.objects.size();++hops) {
        auto owner=std::find_if(original.objects.begin(),original.objects.end(),
            [&](SceneObject const& o){return o.node.id==id;});
        if(owner!=original.objects.end())
            return static_cast<int>(owner-original.objects.begin());
        auto it=std::find_if(composed.objects.begin(),composed.objects.end(),
            [&](SceneObject const& o){return o.node.id==id;});
        if(it==composed.objects.end() || !it->node.parent)break;
        id=it->node.parent;
    }
    return -1;
}
inline Scene load(std::filesystem::path const& root,std::string const& ref) {
    return load_scene(locate(root,ref));
}
} // namespace northstarcpp::editor::visual_prefab
