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

namespace northstarcpp {

struct PhysicsScene::State {
    explicit State(Scene const& source) : snapshot(source), poses(source.objects.size())
    {
        validate_scene(snapshot);
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
            auto const& pose = poses[i];
            snapshot.objects[i].position = {pose.x, pose.y, pose.z};
        }
    }

    Scene snapshot;
    std::vector<NorthStarBodyPose> poses;
    Module module{NORTHSTARCPP_CAPABILITY_PHYSICS};
    NorthStarPhysicsApi const& api = *module.query<NorthStarPhysicsApi>(NORTHSTARCPP_PHYSICS_ABI);
    EcsSceneRuntime ecs;
    NorthStarPhysicsResidencyApi const* residency_api = nullptr;
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
    if (objects.size()!=authored.size() || objects.size()!=state->resident_mask.size())
        throw std::runtime_error("Collision streaming source indices do not match physics world");
    std::size_t changed=0;
    for (std::size_t i=0;i<objects.size();++i) {
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
    (void)state->ecs.reconcile_scene_residency(state->snapshot,scene_world);
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
        ignore_index >= state->snapshot.objects.size())
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
    state->ecs.sync_positions(state->snapshot);
    state->ecs.step(std::min(seconds, 0.1f), 0.f, 0.f);
}

} // namespace northstarcpp
