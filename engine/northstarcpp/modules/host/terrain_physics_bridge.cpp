#include "terrain_physics_bridge.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace northstarcpp {
TerrainPhysicsBridge::TerrainPhysicsBridge(world::TerrainConfig cfg):streamer_(cfg){}
world::TerrainDelta TerrainPhysicsBridge::update(
    PhysicsScene& physics,world::Vec3d camera,world::Vec3d velocity) {
    if(!physics.supports_streamed_terrain())
        throw std::runtime_error("Selected physics provider lacks terrain streaming ABI");
    auto delta=streamer_.update(camera,velocity);
    apply_delta(physics,delta);
    return delta;
}
void TerrainPhysicsBridge::apply_delta(PhysicsScene& physics,
    world::TerrainDelta const& delta) {
    if(!physics.supports_streamed_terrain())
        throw std::runtime_error("Physics provider lacks streaming ABI");
    // Render LOD changes must NOT recook identical collision geometry.
    // A chunk present in both delta sets changed only its visual LOD:
    // keep its existing Jolt mesh and let the renderer replace its visuals.
    std::set<world::ChunkKey> arriving;
    for(auto const& part:delta.uploaded)arriving.insert(part.key);
    bool physics_changed=false;
    for(auto const& key:delta.evicted){
        if(arriving.count(key))continue;
        auto found=std::find(loaded_.begin(),loaded_.end(),key);
        if(found==loaded_.end())continue;
        physics.remove_terrain_chunk(key.x,key.z);
        loaded_.erase(found);
        physics_changed=true;
    }
    for(auto const& part:delta.uploaded){
        if(std::find(loaded_.begin(),loaded_.end(),part.key)!=loaded_.end())
            continue; // same world-space collision shape, new rendering LOD
        std::vector<Vector3> vertices;
        vertices.reserve(part.collision.vertices.size());
        for(auto const& xyz:part.collision.vertices)
            vertices.push_back({xyz.x,xyz.y,xyz.z});
        physics.upsert_terrain_chunk(part.key.x,part.key.z,
                                    vertices,part.collision.indices);
        loaded_.push_back(part.key);
        physics_changed=true;
    }
    if(physics_changed)physics.commit_streamed_terrain();
}
void TerrainPhysicsBridge::unload(PhysicsScene& physics){
    for(auto const& key:loaded_)
        physics.remove_terrain_chunk(key.x,key.z);
    if(!loaded_.empty())physics.commit_streamed_terrain();
    loaded_.clear();
    streamer_.clear();
}
}
