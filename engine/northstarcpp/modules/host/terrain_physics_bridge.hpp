// Host adapter: explicit simulation-thread terrain-to-physics ownership.
// Does not mutate Scene V3, renderer state or plugin ABI 4.
#pragma once
#include <northstarcpp/world_foundation.hpp>
#include <northstarcpp/physics.hpp>
#include <vector>
namespace northstarcpp {
class TerrainPhysicsBridge {
public:
    explicit TerrainPhysicsBridge(world::TerrainConfig config={});
    // Must be called on owning physics/game thread after PhysicsScene exists.
    // Returns the render upload/unload requests for a GPU adapter.
    world::TerrainDelta update(PhysicsScene& physics,world::Vec3d camera,
                               world::Vec3d velocity={});
    void apply_delta(PhysicsScene& physics,world::TerrainDelta const& delta);
    void unload(PhysicsScene& physics);
private:
    world::TerrainStreamer streamer_;
    std::vector<world::ChunkKey> loaded_;
};
}
