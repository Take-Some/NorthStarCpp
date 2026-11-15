// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/physics.hpp>
#include <northstarcpp/scene_world.hpp>
#include <northstarcpp/ecs.hpp>
#include <northstarcpp/physics_plugin.h>
#include "module_loader.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <unordered_set>

namespace northstarcpp {

struct PhysicsScene::State {
    explicit State(Scene const& source)
        : snapshot(source), authored_snapshot(source), poses(source.objects.size()),
          authored_count(source.objects.size())
    {
        validate_scene(snapshot);
        for (auto const& object : snapshot.objects)
            if (object.node.id) used_ids.insert(object.node.id);
        ecs.load(snapshot);
        residency_api = module.query_extension<NorthStarPhysicsResidencyApi>(
            NORTHSTARCPP_PHYSICS_RESIDENCY_MODULE_ABI);
        if (!residency_api) {
            auto const query = module.symbol<NorthStarPhysicsGetResidencyApi>(
                "northstar_physics_get_residency_api");
            auto const* candidate = query ? query(NORTHSTARCPP_PHYSICS_RESIDENCY_ABI) : nullptr;
            if (candidate && valid_api(*candidate))
                residency_api = candidate;
        }
        runtime_api = module.query_extension<NorthStarPhysicsRuntimeApi>(
            NORTHSTARCPP_PHYSICS_RUNTIME_MODULE_ABI);
        if (!runtime_api) {
            auto const query = module.symbol<NorthStarPhysicsGetRuntimeApi>(
                "northstar_physics_get_runtime_api");
            auto const* candidate = query ? query(NORTHSTARCPP_PHYSICS_RUNTIME_ABI) : nullptr;
            if (candidate && candidate->abi_version == NORTHSTARCPP_PHYSICS_RUNTIME_ABI &&
                candidate->add_body && candidate->remove_body)
                runtime_api = candidate;
        }
        resident_mask.assign(snapshot.objects.size(),1);
        std::vector<NorthStarRigidBody> bodies;
        bodies.reserve(snapshot.objects.size());
        for (auto const& object : snapshot.objects)
            bodies.push_back({object.position.x, object.position.y, object.position.z,
                object.size, object.mass, object.player ? 1u : 0u});
        world = api.create(bodies.data(), static_cast<uint32_t>(bodies.size()));
        if (!world)
            throw module.error("Cannot create physics world");
        try {
            read_poses();
        } catch (...) {
            api.destroy(world);
            throw;
        }
    }

    ~State() { api.destroy(world); }

    void read_poses()
    {
        if (!api.read_poses(world, poses.data(), static_cast<uint32_t>(poses.size())))
            throw module.error("Cannot read physics poses");
        for (auto const& pose : poses) {
            if (!std::isfinite(pose.x) || !std::isfinite(pose.y) || !std::isfinite(pose.z) ||
                !std::all_of(std::begin(pose.rotation), std::end(pose.rotation),
                    [](float value) { return std::isfinite(value); }))
                throw module.error("Physics module returned an invalid pose");
        }
        for (std::size_t i = 0; i < poses.size(); ++i) {
            if (i >= authored_count && !runtime_alive[i - authored_count])
                continue;
            auto const& pose = poses[i];
            snapshot.objects[i].position = {pose.x, pose.y, pose.z};
            if (i < authored_count)
                authored_snapshot.objects[i].position = snapshot.objects[i].position;
        }
    }

    Scene snapshot;
    Scene authored_snapshot;
    std::size_t authored_count = 0;
    std::vector<uint8_t> runtime_alive;
    std::unordered_set<std::uint64_t> used_ids;
    std::vector<NorthStarBodyPose> poses;
    Module module{NORTHSTARCPP_CAPABILITY_PHYSICS};
    NorthStarPhysicsApi const& api = *module.query<NorthStarPhysicsApi>(NORTHSTARCPP_PHYSICS_ABI);
    EcsSceneRuntime ecs;
    NorthStarPhysicsResidencyApi const* residency_api = nullptr;
    NorthStarPhysicsRuntimeApi const* runtime_api = nullptr;
    std::vector<uint8_t> resident_mask;
    void* world = nullptr;
};

PhysicsScene::PhysicsScene(Scene const& source) : state(std::make_unique<State>(source)) {}
PhysicsScene::~PhysicsScene() = default;
Scene const& PhysicsScene::scene() const { return state->snapshot; }
std::string PhysicsScene::provider_name() const { return state->module.provider(); }
std::string PhysicsScene::provider_version() const { return state->module.version(); }

northstarcpp::math::float4x4 PhysicsScene::orientation(std::size_t index) const
{
    auto const& r = state->poses.at(index).rotation;
    using northstarcpp::math::float4;
    return {float4(r[0], r[1], r[2], 0.f), float4(r[3], r[4], r[5], 0.f),
        float4(r[6], r[7], r[8], 0.f), float4(0.f, 0.f, 0.f, 1.f)};
}

bool PhysicsScene::supports_scene_residency() const
{
    return state->residency_api!=nullptr;
}
bool PhysicsScene::supports_runtime_entities() const
{
    return state->runtime_api != nullptr;
}
std::size_t PhysicsScene::authored_object_count() const { return state->authored_count; }
Scene const& PhysicsScene::authored_scene() const { return state->authored_snapshot; }
bool PhysicsScene::runtime_alive(std::size_t index) const
{
    return index >= state->authored_count &&
        index - state->authored_count < state->runtime_alive.size() &&
        state->runtime_alive[index - state->authored_count] != 0;
}

std::size_t PhysicsScene::spawn_runtime(SceneObject const& object)
{
    if (!state->runtime_api)
        throw std::runtime_error("Active physics provider lacks runtime-body extension");
    if (object.player || object.name.empty() || object.name.size() >= NORTHSTARCPP_ECS_NAME_BYTES ||
        !object.node.id || object.node.cell.size() > 128 ||
        !std::isfinite(object.position.x) || !std::isfinite(object.position.y) ||
        !std::isfinite(object.position.z) || !std::isfinite(object.size) ||
        !std::isfinite(object.mass) || object.size <= 0.f || object.size > 1000.f ||
        object.mass < 0.f || object.mass > 1000000.f ||
        std::max({std::abs(object.position.x),std::abs(object.position.y),
                  std::abs(object.position.z)}) > 1000000.f ||
        state->snapshot.objects.size() >= max_scene_objects)
        throw std::invalid_argument("Invalid runtime scene entity");
    if (state->used_ids.find(object.node.id)!=state->used_ids.end())
        throw std::invalid_argument("Duplicate runtime entity stable ID");
    // Allocate host snapshot storage first. On a provider failure, roll back.
    auto const index = state->snapshot.objects.size();
    state->snapshot.objects.push_back(object);
    try {
        state->poses.push_back({});
        state->resident_mask.push_back(1);
        state->runtime_alive.push_back(1);
    } catch (...) {
        state->snapshot.objects.pop_back();
        if (state->poses.size() > index) state->poses.pop_back();
        if (state->resident_mask.size() > index) state->resident_mask.pop_back();
        if (state->runtime_alive.size() > index-state->authored_count)
            state->runtime_alive.pop_back();
        throw;
    }
    NorthStarRigidBody const body{
        object.position.x, object.position.y, object.position.z,
        object.size, object.mass, 0u};
    uint32_t result = UINT32_MAX;
    if (!state->runtime_api->add_body(state->world, &body, &result)) {
        state->snapshot.objects.pop_back();
        state->poses.pop_back();
        state->resident_mask.pop_back();
        state->runtime_alive.pop_back();
        throw state->module.error("Cannot spawn runtime physics body");
    }
    if (result != index)
        throw std::runtime_error("Runtime physics provider violated stable scene index contract");
    state->used_ids.insert(object.node.id);
    // Initialize only the new pose instead of reading every provider body.
    // PhysicsScene::step updates all dynamic poses normally on the next tick.
    state->poses[index] = {object.position.x, object.position.y, object.position.z,
        {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f}};
    return index;
}

void PhysicsScene::destroy_runtime(std::size_t index)
{
    if (!runtime_alive(index))
        throw std::out_of_range("Runtime scene entity is stale or authored");
    if (!state->runtime_api->remove_body(state->world,static_cast<uint32_t>(index)))
        throw state->module.error("Cannot destroy runtime physics body");
    state->runtime_alive[index-state->authored_count] = 0;
    // Keep the slot but never render or query it: authored indices must remain
    // stable, including during World Partition updates.
    auto& dead = state->snapshot.objects[index];
    dead.node.visible = false;
    dead.node.collision = false;
    dead.node.asset.clear();
}

std::size_t PhysicsScene::ecs_resident_entity_count() const
{
    return state->ecs.size();
}

std::size_t PhysicsScene::sync_scene_residency(
    SceneWorld const& scene_world, Vector3 protected_position)
{
    // The ECS provider is independent of the physics collision residency
    // extension. An older physics DLL retains safe, eager collision fallback.
    if (!std::isfinite(protected_position.x) ||
        !std::isfinite(protected_position.y) ||
        !std::isfinite(protected_position.z))
        throw std::invalid_argument("Invalid protected position for collision streaming");
    auto const& objects=state->snapshot.objects;
    auto const& authored=scene_world.source().objects;
    if (objects.size()<authored.size() ||
        state->authored_count!=authored.size() ||
        objects.size()!=state->resident_mask.size())
        throw std::runtime_error("Collision streaming source indices do not match physics world");
    std::size_t changed=0;
    for (std::size_t i=0;i<authored.size();++i) {
        auto const& object=objects[i];
        auto const& node=authored[i].node;
        if (node.id!=object.node.id)
            throw std::runtime_error("Collision streaming stable ID mismatch");
        // Never detach dynamic traffic/NPCs or their player controller.
        if (object.player || object.mass>0.f)
            continue;
        // Preserve static world surfaces with no sector assignment (terrain,
        // persistent roads and other collision assets).
        if (node.cell.empty() && node.collision)
            continue;
        auto const dx=object.position.x-protected_position.x;
        auto const dy=object.position.y-protected_position.y;
        auto const dz=object.position.z-protected_position.z;
        auto const protect=std::max(32.f,object.size*2.f);
        bool const nearby=dx*dx+dy*dy+dz*dz <= protect*protect;
        bool const desired=node.collision &&
            (nearby || scene_world.object_resident(i));
        if (state->resident_mask[i] == static_cast<uint8_t>(desired))
            continue;
        if (state->residency_api) {
            if (!state->residency_api->set_static_resident(
                    state->world,static_cast<uint32_t>(i),desired ? 1u : 0u))
                throw state->module.error("Cannot change static collision residency");
            state->resident_mask[i]=static_cast<uint8_t>(desired);
            ++changed;
        }
    }
    (void)state->ecs.reconcile_scene_residency(state->authored_snapshot,scene_world);
    return changed;
}

void PhysicsScene::jump()
{
    if (!state->api.jump || !state->api.jump(state->world))
        throw state->module.error("Cannot jump physics player");
}

void PhysicsScene::apply_impulse(std::size_t index, Vector3 impulse)
{
    auto const& objects=state->snapshot.objects;
    if (index>=objects.size() || objects[index].mass<=0.f || objects[index].player ||
        (index>=state->authored_count && !runtime_alive(index)) ||
        !std::isfinite(impulse.x) || !std::isfinite(impulse.y) ||
        !std::isfinite(impulse.z) ||
        std::hypot(impulse.x,impulse.y,impulse.z)>100.f)
        throw std::invalid_argument("Invalid physics impulse or dynamic body index");
    if(!state->api.apply_impulse ||
        !state->api.apply_impulse(state->world,static_cast<uint32_t>(index),
                                  impulse.x,impulse.y,impulse.z))
        throw state->module.error("Cannot apply physics impulse");
}

void PhysicsScene::move_static(std::size_t index, Vector3 position)
{
    if (index >= state->snapshot.objects.size() ||
        (index>=state->authored_count && !runtime_alive(index)) ||
        state->snapshot.objects[index].mass != 0.f ||
        !std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z))
        throw std::runtime_error("Invalid static physics relocation");
    if (!state->api.move_static || !state->api.move_static(
            state->world, static_cast<uint32_t>(index), position.x, position.y, position.z))
        throw state->module.error("Cannot move static physics object");
    state->snapshot.objects[index].position = position;
}

std::optional<PhysicsCastHit> PhysicsScene::sweep_sphere(
    Vector3 start, Vector3 end, float radius, std::size_t ignore_index) const
{
    auto finite=[](Vector3 const& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite(start) || !finite(end) || !std::isfinite(radius) ||
        radius<=0.f || radius>2.f)
        throw std::invalid_argument("Invalid physics sphere cast");
    if (ignore_index!=static_cast<std::size_t>(-1) &&
        (ignore_index >= state->snapshot.objects.size() ||
         (ignore_index>=state->authored_count && !runtime_alive(ignore_index))))
        throw std::out_of_range("Invalid ignored physics body index");
    std::uint32_t const ignored=ignore_index==static_cast<std::size_t>(-1)
        ? UINT32_MAX : static_cast<std::uint32_t>(ignore_index);
    NorthStarPhysicsCastHit hit{};
    if (!state->api.sweep_sphere ||
        !state->api.sweep_sphere(state->world,start.x,start.y,start.z,
            end.x,end.y,end.z,radius,ignored,&hit))
        throw state->module.error("Physics sphere sweep failed");
    if (!hit.hit) return std::nullopt;
    if (!std::isfinite(hit.fraction) || hit.fraction<0.f ||
        hit.fraction>1.f || !std::isfinite(hit.normal_x) ||
        !std::isfinite(hit.normal_y) || !std::isfinite(hit.normal_z))
        throw std::runtime_error("Physics provider returned invalid sweep hit");
    return PhysicsCastHit{hit.fraction,
        {hit.normal_x,hit.normal_y,hit.normal_z},hit.object_index};
}

void PhysicsScene::add_static_mesh(std::vector<Vector3> const& vertices,
                                  std::vector<std::uint32_t> const& triangles)
{
    if (vertices.size()<3 || vertices.size()>1000000 ||
        triangles.size()<3 || triangles.size()>3000000 ||
        triangles.size()%3!=0)
        throw std::invalid_argument("Invalid static triangle mesh size");
    std::vector<float> flat;
    flat.reserve(vertices.size()*3);
    for (auto const& v : vertices) {
        if (!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
            throw std::invalid_argument("Static mesh contains non-finite vertex");
        flat.insert(flat.end(),{v.x,v.y,v.z});
    }
    for(auto const index:triangles)
        if(index>=vertices.size())
            throw std::invalid_argument("Static mesh index out of range");
    if (!state->api.add_static_mesh ||
        !state->api.add_static_mesh(state->world,flat.data(),
            static_cast<std::uint32_t>(vertices.size()),triangles.data(),
            static_cast<std::uint32_t>(triangles.size())))
        throw state->module.error("Physics static triangle registration failed");
}

void PhysicsScene::step(float seconds, float input_x, float input_z)
{
    if (!std::isfinite(seconds) || seconds <= 0.f)
        return;
    if (!state->api.step(state->world, seconds, input_x, input_z))
        throw state->module.error("Cannot update physics world");
    state->read_poses();
    state->ecs.sync_positions(state->authored_snapshot);
    state->ecs.step(std::min(seconds, 0.1f), 0.f, 0.f);
}

} // namespace northstarcpp
