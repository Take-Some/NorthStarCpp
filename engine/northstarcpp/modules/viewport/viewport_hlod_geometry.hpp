// Native HLOD source extraction: actual world-space meshes and material slots.
// The renderer retains AABB fallback if any dependency isn't loaded.
#pragma once
#include <northstarcpp/scene_hlod_baker.hpp>
#include <northstarcpp/scene_world.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
namespace northstarcpp::viewport_detail {
inline std::array<float,12> hlod_world_matrix(SceneObject const& o) {
    constexpr float radians=0.01745329251994329577f;
    float const x=o.node.rotation.x*radians,y=o.node.rotation.y*radians,
        z=o.node.rotation.z*radians;
    float const sx=std::sin(x),cx=std::cos(x),
        sy=std::sin(y),cy=std::cos(y),
        sz=std::sin(z),cz=std::cos(z);
    float const b[9]{
        cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx,
        sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx,
        -sy,cy*sx,cy*cx};
    auto const scales=std::array<float,3>{
        o.size*o.node.scale.x,o.size*o.node.scale.y,o.size*o.node.scale.z};
    return {b[0]*scales[0],b[1]*scales[1],b[2]*scales[2],o.position.x,
            b[3]*scales[0],b[4]*scales[1],b[5]*scales[2],o.position.y,
            b[6]*scales[0],b[7]*scales[1],b[8]*scales[2],o.position.z};
}
struct HlodSourceBundle {
    std::vector<scene_hlod::Source> sources;
    std::string cache_key;
    std::size_t source_indices=0;
};
template<class MaterialLookup,class SkinLookup>
bool prepare_hlod_sources(
    Scene const& scene,SceneHlodProxy const& proxy,
    std::unordered_map<std::string,MeshAsset> const& loaded,
    MaterialLookup material,SkinLookup skinned,
    HlodSourceBundle& out,std::uintptr_t world_identity,
    std::size_t max_indices=300000u)
{
    out.sources.clear();out.cache_key.clear();out.source_indices=0;
    if(proxy.members.empty() || proxy.members.size()>128)return false;
    auto put=[&](auto const& value) {
        out.cache_key.append(reinterpret_cast<char const*>(&value),sizeof(value));
    };
    put(world_identity);put(proxy.level);
    for(auto const i:proxy.members) {
        if(i>=scene.objects.size())return false;
        auto const& object=scene.objects[i];
        auto const& ref=object.node.asset;
        if(ref.empty() || object.mass!=0.f || skinned(ref,object.node.id))
            return false;
        auto const it=loaded.find(ref);
        if(it==loaded.end() || it->second.indices.empty())return false;
        if(it->second.indices.size()>max_indices-out.source_indices)return false;
        auto const transform=hlod_world_matrix(object);
        std::string const override_ref=material(object.node.id);
        put(object.node.id);put(i);put(object.color);
        auto const size=ref.size();put(size);out.cache_key.append(ref);
        auto const material_size=override_ref.size();put(material_size);
        out.cache_key.append(override_ref);
        out.cache_key.append(reinterpret_cast<char const*>(transform.data()),
                             transform.size()*sizeof(float));
        out.sources.push_back({&it->second,transform,override_ref,true});
        out.source_indices+=it->second.indices.size();
    }
    return !out.sources.empty();
}
// Reuse baked geometry across camera frames. No GPU/asset I/O in this path;
// if any source is unavailable, the caller may render its original AABB.
template<class MaterialLookup,class SkinLookup>
MeshAsset const* resolve_hlod_mesh(Scene const& scene,
    SceneHlodProxy const& proxy,
    std::unordered_map<std::string,MeshAsset> const& loaded,
    std::unordered_map<std::string,MeshAsset>& baked_cache,
    MaterialLookup material,SkinLookup skinned,std::uintptr_t world_identity)
{
    HlodSourceBundle sources;
    if(!prepare_hlod_sources(scene,proxy,loaded,material,skinned,
                             sources,world_identity))return nullptr;
    auto existing=baked_cache.find(sources.cache_key);
    if(existing!=baked_cache.end())return &existing->second;
    if(baked_cache.size()>=128)return nullptr;
    // Explicit bounded derived-mesh memory, not merely a count of clusters.
    constexpr std::size_t budget=128u*1024u*1024u;
    std::size_t used=0;
    for(auto const& entry:baked_cache) {
        used+=entry.second.vertices.size()*sizeof(MeshVertex)+
              entry.second.indices.size()*sizeof(std::uint32_t);
    }
    if(used>=budget)return nullptr;
    try {
        auto baked=scene_hlod::bake(sources.sources,300000u);
        auto const bytes=baked.mesh.vertices.size()*sizeof(MeshVertex)+
                         baked.mesh.indices.size()*sizeof(std::uint32_t);
        if(bytes>budget-used)return nullptr;
        if(baked.mesh.indices.empty())return nullptr;
        auto pair=baked_cache.try_emplace(std::move(sources.cache_key),
                                          std::move(baked.mesh));
        return &pair.first->second;
    }catch(std::exception const&) {
        // Conservative fallback: never render partial/incorrect geometry.
        return nullptr;
    }
}
} // namespace northstarcpp::viewport_detail
