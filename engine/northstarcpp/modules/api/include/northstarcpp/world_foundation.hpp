// NorthStarCpp - original native world foundation for procedural terrain and CityRoot.
// Copyright (c) 2026 Kayla's Systems. C++17, no renderer/physics provider dependency.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>
#include <future>
#include <optional>

namespace northstarcpp::world {
struct Vec3d { double x=0., y=0., z=0.; };
struct Vec3f { float x=0.f, y=0.f, z=0.f; };
struct TerrainVertex {
    Vec3f position{}; // relative to chunk anchor: stable GPU precision
    Vec3f normal{0.f,1.f,0.f};
    float u=0.f, v=0.f;
};
struct TerrainMesh {
    std::vector<TerrainVertex> vertices;
    std::vector<std::uint32_t> indices;
};
struct CollisionMesh {
    // World coordinates; collision chunks are independent of visual LOD.
    std::vector<Vec3f> vertices;
    std::vector<std::uint32_t> indices;
};
struct ChunkKey {
    std::int32_t x=0, z=0;
    bool operator<(ChunkKey const& r) const noexcept {
        return x<r.x || (x==r.x && z<r.z);
    }
    bool operator==(ChunkKey const& r) const noexcept {
        return x==r.x && z==r.z;
    }
};
struct TerrainConfig {
    std::uint32_t seed=20261117u;
    std::uint32_t quads_per_axis=32;
    std::uint32_t collision_quads=16;
    std::uint32_t radius=3; // 7x7 chunks maximum
    std::uint32_t max_lod=3;
    double width_m=64.0, vertical_scale_m=9.0, base_height_m=0.0;
    double skirt_depth_m=2.0;
    void validate() const;
};
struct TerrainChunk {
    ChunkKey key{};
    std::uint32_t lod=0;
    Vec3d anchor{}; // render local-to-world translation
    TerrainMesh render{};
    CollisionMesh collision{};
};
struct TerrainDelta {
    std::vector<ChunkKey> evicted;
    std::vector<TerrainChunk> uploaded;
    std::size_t resident_count=0;
    std::uint64_t generation=0;
};
class TerrainProvider {
public:
    explicit TerrainProvider(TerrainConfig config={});
    TerrainConfig const& config() const noexcept {return config_;}
    double height(double world_x,double world_z) const;
    Vec3f normal(double world_x,double world_z) const;
    TerrainChunk generate(ChunkKey key,std::uint32_t lod) const;
private:
    TerrainConfig config_;
};
class TerrainStreamer {
public:
    explicit TerrainStreamer(TerrainConfig config={});
    // Update at the simulation scheduling boundary; load/unload changes are
    // passed to a renderer/physics sink by the caller, never from worker threads.
    TerrainDelta update(Vec3d camera,Vec3d velocity={});
    std::size_t resident_count() const noexcept {return resident_.size();}
    TerrainProvider const& terrain() const noexcept {return provider_;}
    void clear() noexcept {resident_.clear();generation_=0;focus_center_.reset();}
private:
    TerrainProvider provider_;
    std::map<ChunkKey,std::uint32_t> resident_;
    std::optional<ChunkKey> focus_center_;
    std::uint64_t generation_=0;
};
// Async chunk generator: the first ring is prepared synchronously at startup
// to avoid leaving the Pawn over a hole. Every subsequent ring transition
// is generated on a worker, never on the game's simulation/render frame.
// update() polls a future without waiting and coalesces intermediate moves.
class TerrainAsyncStreamer {
public:
    explicit TerrainAsyncStreamer(TerrainConfig config={});
    TerrainAsyncStreamer(TerrainAsyncStreamer const&)=delete;
    TerrainAsyncStreamer& operator=(TerrainAsyncStreamer const&)=delete;
    ~TerrainAsyncStreamer();
    std::optional<TerrainDelta> update(Vec3d focus,Vec3d velocity={});
    bool pending() const noexcept {return task_.valid();}
private:
    TerrainStreamer streamer_;
    std::future<TerrainDelta> task_;
    ChunkKey launched_center_{};
    bool initialized_=false;
    ChunkKey center(Vec3d focus,Vec3d velocity) const;
};
// Foundation for an atomic runtime rebase. Caller must shift every physical
// body, scene transform and camera together before committing this origin.
class FloatingOrigin {
public:
    explicit FloatingOrigin(double threshold_m=512.,double grid_m=64.);
    Vec3d origin() const noexcept {return origin_;}
    Vec3d to_local(Vec3d absolute) const noexcept;
    Vec3d to_absolute(Vec3d local) const noexcept;
    std::optional<Vec3d> proposal(Vec3d absolute_focus) const;
    void commit(Vec3d new_origin);
private:
    Vec3d origin_{};
    double threshold_=512.,grid_=64.;
};
struct ActorState {
    Vec3d position{}; // position in local CityRoot coordinates
    Vec3d velocity{}; // relative to support surface in CityRoot coordinates
};
class CityRoot {
public:
    explicit CityRoot(std::uint64_t stable_id);
    std::uint64_t id() const noexcept {return id_;}
    void set_motion(Vec3d position,double yaw_rad,Vec3d linear_velocity,
                    double yaw_rate_rad_per_second);
    void advance(double fixed_dt); // deterministic physics tick
    Vec3d position() const noexcept {return position_;}
    double yaw() const noexcept {return yaw_;}
    Vec3d to_world(Vec3d local) const noexcept;
    Vec3d to_local(Vec3d global) const noexcept;
    Vec3d rotate_to_world(Vec3d local_dir) const noexcept;
    Vec3d rotate_to_local(Vec3d global_dir) const noexcept;
    Vec3d support_velocity(Vec3d point_world) const noexcept;
    ActorState transfer_to(CityRoot const& other,ActorState actor) const noexcept;
private:
    std::uint64_t id_;
    Vec3d position_{};
    Vec3d velocity_{};
    double yaw_=0.,yaw_rate_=0.;
};
struct GpuInstance {
    std::uint64_t stable_id=0;
    Vec3d local_position{};
    double local_yaw=0.;
};
struct GpuTransform {
    std::uint64_t stable_id=0;
    // Row-vector 4x4, for render_temporal_instances_api.h.
    std::array<float,16> world{};
};
class CityGpuTransformBatch {
public:
    explicit CityGpuTransformBatch(std::uint64_t root_id);
    void set_instances(std::vector<GpuInstance> instances);
    // Immutable mesh/index buffers are not touched here. Emits transforms
    // only when root or member inputs have changed.
    std::vector<GpuTransform> collect_dirty(CityRoot const& root);
    std::size_t instance_count() const noexcept {return members_.size();}
private:
    std::uint64_t root_id_;
    std::vector<GpuInstance> members_;
    std::vector<GpuTransform> previous_;
};
} // namespace northstarcpp::world
