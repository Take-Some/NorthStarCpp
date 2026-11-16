// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/physics.hpp>
#include <northstarcpp/player_controller.hpp>
#include <northstarcpp/player_blueprint.hpp>
#include <northstarcpp/player_animation_controller.hpp>
#include <northstarcpp/scene_pawn.hpp>
#include <northstarcpp/collision_authoring.hpp>
#include <northstarcpp/collision_mesh.hpp>
#include <northstarcpp/collision_hulls.hpp>
#include <northstarcpp/scene_world.hpp>
#include <northstarcpp/ecs.hpp>
#include <northstarcpp/physics_plugin.h>
#include <northstarcpp/physics_surface.hpp>
#include "module_loader.hpp"
// Collision authoring contract is provided by <northstarcpp/collision_authoring.hpp>.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <unordered_set>
#include <unordered_map>

namespace northstarcpp {

struct PhysicsScene::State {
    explicit State(Scene const& source)
        : snapshot(source), authored_snapshot(source), poses(source.objects.size()),
          authored_count(source.objects.size())
    {
        validate_scene(snapshot);
        for (auto const& object : snapshot.objects)
            if (object.node.id) used_ids.insert(object.node.id);
        controller.auto_possess(snapshot);
        initialize_player_attachments();
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
        collider_api = module.query_extension<NorthStarPhysicsColliderApi>(
            NORTHSTARCPP_PHYSICS_COLLIDER_MODULE_ABI);
        if (!collider_api) {
            auto const query = module.symbol<NorthStarPhysicsGetColliderApi>(
                "northstar_physics_get_collider_api");
            auto const* candidate = query ? query(NORTHSTARCPP_PHYSICS_COLLIDER_ABI) : nullptr;
            if (candidate && candidate->abi_version==NORTHSTARCPP_PHYSICS_COLLIDER_ABI &&
                candidate->configure)
                collider_api = candidate;
        }
        surface_api = module.query_extension<NorthStarPhysicsSurfaceApi>(
            NORTHSTARCPP_PHYSICS_SURFACE_MODULE_ABI);
        if(!surface_api) {
            auto const query=module.symbol<NorthStarPhysicsGetSurfaceApi>(
                "northstar_physics_get_surface_api");
            auto const* candidate=query?query(NORTHSTARCPP_PHYSICS_SURFACE_ABI):nullptr;
            if(candidate && candidate->abi_version==NORTHSTARCPP_PHYSICS_SURFACE_ABI &&
               candidate->set_triangle_materials &&
               candidate->add_static_mesh_materials &&
               candidate->sweep_sphere_surface)
                surface_api=candidate;
        }
        mesh_api = module.query_extension<NorthStarPhysicsMeshApi>(
            NORTHSTARCPP_PHYSICS_MESH_MODULE_ABI);
        cooked_hulls_api=module.query_extension<NorthStarPhysicsCookedHullsApi>(
            NORTHSTARCPP_PHYSICS_COOKED_HULLS_MODULE_ABI);
        response_api=module.query_extension<NorthStarPhysicsResponseApi>(
            NORTHSTARCPP_PHYSICS_RESPONSE_MODULE_ABI);
        channels_api=module.query_extension<NorthStarPhysicsChannelsApi>(
            NORTHSTARCPP_PHYSICS_CHANNELS_MODULE_ABI);
        character_api = module.query_extension<NorthStarPhysicsCharacterApi>(
            NORTHSTARCPP_PHYSICS_CHARACTER_MODULE_ABI);
        vehicle_api = module.query_extension<NorthStarPhysicsVehicleApi>(
            NORTHSTARCPP_PHYSICS_VEHICLE_MODULE_ABI);
        resident_mask.assign(snapshot.objects.size(),1);
        std::vector<NorthStarRigidBody> bodies;
        bodies.reserve(snapshot.objects.size());
        for (auto const& object : snapshot.objects)
            bodies.push_back({object.position.x, object.position.y, object.position.z,
                object.size,object.mass,
                controller.controlled_entity()==object.node.id ? 1u : 0u});
        world = api.create(bodies.data(), static_cast<uint32_t>(bodies.size()));
        if (!world)
            throw module.error("Cannot create physics world");
        try {
            for(std::size_t index=0;index<snapshot.objects.size();++index) {
                auto const& object=snapshot.objects[index];
                auto const custom=collision_authoring::lookup(snapshot,object.node.id);
                if(!custom && object.node.collision)continue;
                if(!collider_api)
                    throw std::runtime_error(
                        "Active physics provider does not support authored colliders");
                auto const settings=custom.value_or(
                    collision_authoring::defaults(object.size));
                NorthStarPhysicsCollider collider{
                    object.node.collision?1u:0u,
                    static_cast<std::uint32_t>(settings.shape)>=3u?0u:
                        static_cast<std::uint32_t>(settings.shape),
                    settings.dimensions.x,settings.dimensions.y,settings.dimensions.z,
                    settings.offset.x,settings.offset.y,settings.offset.z};
                if(!collider_api->configure(world,static_cast<std::uint32_t>(index),
                                           &collider))
                    throw module.error("Physics provider rejected authored collider");
                std::uint8_t responses[16]{};
                bool custom_responses=false;
                for(std::size_t i=0;i<16;++i) {
                    responses[i]=static_cast<std::uint8_t>(settings.responses[i]);
                    custom_responses|=responses[i]!=2;
                }
                if(channels_api) {
                    if(!channels_api->configure(world,std::uint32_t(index),
                            static_cast<std::uint32_t>(settings.object_type),responses,16))
                        throw module.error("Physics provider rejected custom channels");
                } else if(response_api) {
                    if(static_cast<std::uint32_t>(settings.object_type)>=6)
                        throw std::runtime_error("Physics provider lacks custom channels");
                    for(std::size_t n=6;n<16;++n)
                        if(responses[n]!=2)
                            throw std::runtime_error("Physics provider lacks custom channel responses");
                    if(!response_api->configure(world,std::uint32_t(index),
                            static_cast<std::uint32_t>(settings.object_type),responses))
                        throw module.error("Physics provider rejected collision response matrix");
                } else if(custom_responses) {
                    throw std::runtime_error(
                        "Physics provider lacks Block/Overlap/Ignore response matrix");
                } else if(settings.object_type!=collision_authoring::ObjectType::WorldStatic) {
                    if(!mesh_api ||
                       !mesh_api->set_object_type(world,std::uint32_t(index),
                           static_cast<std::uint32_t>(settings.object_type)))
                        throw std::runtime_error("Physics provider lacks Object Type support");
                }
                if(settings.shape==collision_authoring::Shape::ComplexMesh ||
                   settings.shape==collision_authoring::Shape::AutoConvex) {
                    if(settings.shape==collision_authoring::Shape::ComplexMesh &&
                       object.mass>0.f)
                        throw std::invalid_argument(
                            "Complex Mesh collision is static-only; use Auto Convex for dynamic");
                    auto mesh=collision_mesh::lookup(snapshot,object.node.id);
                    if(!mesh || mesh->source!=object.node.asset)
                        throw std::runtime_error(
                            "Collision mesh absent/stale; regenerate from the selected model");
                    if(!mesh_api)throw std::runtime_error(
                        "Physics provider lacks complex mesh collision extension");
                    bool loaded_cooked=false;
                    if(settings.shape==collision_authoring::Shape::AutoConvex &&
                       cooked_hulls_api) {
                        auto baked=collision_hulls::lookup(snapshot,object.node.id);
                        if(baked && baked->source==object.node.asset &&
                           baked->splits_num==settings.splits_num) {
                            std::vector<std::vector<float>> positions;
                            std::vector<NorthStarPhysicsCookedHull> descriptors;
                            positions.reserve(baked->hulls.size());
                            descriptors.reserve(baked->hulls.size());
                            for(auto const& hull:baked->hulls) {
                                positions.push_back(collision_hulls::transformed(
                                    hull,object,settings,true));
                                descriptors.push_back({positions.back().data(),
                                    std::uint32_t(positions.back().size()/3),
                                    hull.indices.data(),std::uint32_t(hull.indices.size())});
                            }
                            if(!cooked_hulls_api->configure_hulls(world,
                                std::uint32_t(index),descriptors.data(),
                                std::uint32_t(descriptors.size())))
                                throw module.error("Jolt rejected cached V-HACD hulls");
                            loaded_cooked=true;
                        }
                    }
                    if(!loaded_cooked) {
                        // Backward-compatibility only: previous scenes are cooked
                        // at runtime; regenerate to cache hulls and reduce startup.
                        auto xyz=collision_mesh::transform(*mesh,object,settings,true);
                        if(!mesh_api->configure_mesh(world,std::uint32_t(index),
                            static_cast<std::uint32_t>(settings.shape),
                            settings.splits_num,xyz.data(),std::uint32_t(xyz.size()/3),
                            mesh->indices.data(),std::uint32_t(mesh->indices.size())))
                            throw module.error("Physics provider failed mesh cooking");
                    }
                    if(!mesh->triangle_materials.empty() && surface_api &&
                       settings.shape==collision_authoring::Shape::ComplexMesh &&
                       !surface_api->set_triangle_materials(
                           world,static_cast<uint32_t>(index),
                           mesh->triangle_materials.data(),
                           static_cast<uint32_t>(mesh->triangle_materials.size())))
                        throw module.error("Physics provider rejected triangle materials");
                }
            }
            if(controller.possessed()) {
                if(!character_api || !character_api->possess_body ||
                   !character_api->configure_motor || !character_api->set_sprinting)
                    throw std::runtime_error("Selected physics provider "+module.provider()+
                        " lacks Pawn possession API");
                auto const index=controller.controlled_index(snapshot);
                if(!index)throw std::runtime_error("Controller Pawn not in physics world");
                auto const tune=gameplay::motor_settings(snapshot,controller.controlled_entity());
                NorthStarPhysicsCharacterMotor motor{tune.walk_speed,tune.run_speed,
                    tune.jump_speed,tune.acceleration,tune.air_control,tune.gravity_scale};
                if(!character_api->configure_motor(world,std::uint32_t(*index),&motor) ||
                   !character_api->possess_body(world,std::uint32_t(*index)))
                    throw module.error("Failed to initialize Jolt Pawn");
            }
            read_poses();
            initialize_player_animation();
        } catch (...) {
            api.destroy(world);
            throw;
        }
    }

    ~State() { api.destroy(world); }

    struct AttachedEntity {
        std::size_t child=0,parent=0;
        Vector3 offset{};
    };
    std::vector<AttachedEntity> player_attachments;
    void initialize_player_attachments() {
        std::unordered_map<std::uint64_t,std::size_t> index;
        index.reserve(snapshot.objects.size());
        for(std::size_t i=0;i<snapshot.objects.size();++i)
            index.emplace(snapshot.objects[i].node.id,i);
        for(std::size_t i=0;i<snapshot.objects.size();++i) {
            auto const& child=snapshot.objects[i];
            if(!child.node.parent)continue;
            auto found=index.find(child.node.parent);
            if(found==index.end()||
               !player_character::has_pawn(snapshot,child.node.parent))
                continue;
            if(player_blueprint::find(snapshot,child.node.id,
                    player_blueprint::camera_attachment_type)) {
                // Keep the authored camera offset and independent rig
                // position instead of forcing camera height from Pawn.
                auto const& pawn=snapshot.objects[found->second];
                player_attachments.push_back({i,found->second,
                    {child.position.x-pawn.position.x,
                     child.position.y-pawn.position.y,
                     child.position.z-pawn.position.z}});
            } else if(player_blueprint::find(snapshot,child.node.id,
                    player_character::motor_type)) {
                // CharacterMotor is its own logical Scene Entity. Its spatial
                // anchor follows the possessed Pawn, not a second physics body.
                player_attachments.push_back({i,found->second,{0.f,0.f,0.f}});
            } else if(player_blueprint::find(snapshot,child.node.id,
                    player_blueprint::mesh_type)) {
                auto const options=player_blueprint::mesh(snapshot,child.node.id);
                player_attachments.push_back({i,found->second,options.offset});
            }
        }
    }

    void initialize_player_animation() {
        player_animator.reset();
        animation_pawn_id=0;
        animation_mesh_id=0;
        if(!controller.possessed())return;
        auto const pawn=controller.controlled_entity();
        auto const links=player_blueprint::layout(snapshot,pawn);
        if(!links||!links->mesh)return;
        player_animator.emplace(player_blueprint::animation(snapshot,links->mesh));
        animation_pawn_id=pawn;
        animation_mesh_id=links->mesh;
    }

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
        // Camera and SkeletalMesh follow the Pawn's live physics pose.
        // Attachments are indexed at construction; no per-frame scene scans.
        for(auto const& attachment:player_attachments) {
            auto const& pawn=snapshot.objects[attachment.parent].position;
            auto const& delta=attachment.offset;
            auto const position=Vector3{pawn.x+delta.x,pawn.y+delta.y,pawn.z+delta.z};
            snapshot.objects[attachment.child].position=position;
            if(attachment.child<authored_count)
                authored_snapshot.objects[attachment.child].position=position;
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
    NorthStarPhysicsColliderApi const* collider_api = nullptr;
    NorthStarPhysicsMeshApi const* mesh_api = nullptr;
    NorthStarPhysicsCookedHullsApi const* cooked_hulls_api = nullptr;
    NorthStarPhysicsResponseApi const* response_api = nullptr;
    NorthStarPhysicsChannelsApi const* channels_api = nullptr;
    NorthStarPhysicsSurfaceApi const* surface_api = nullptr;
    NorthStarPhysicsVehicleApi const* vehicle_api = nullptr;
    NorthStarPhysicsCharacterApi const* character_api = nullptr;
    gameplay::PlayerController controller;
    std::optional<player_animation::Controller> player_animator;
    std::uint64_t animation_pawn_id=0;
    std::uint64_t animation_mesh_id=0;
    std::unordered_set<std::uint32_t> native_vehicles;
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
bool PhysicsScene::supports_collision_response_matrix() const {
    return state->response_api!=nullptr;
}
std::vector<PhysicsContactEvent> PhysicsScene::poll_collision_events(
    std::size_t max_events) {
    if(max_events>4096)
        throw std::invalid_argument("Collision event drain exceeds safe budget");
    if(!state->response_api || !max_events)return {};
    std::vector<NorthStarPhysicsContactEvent> events(max_events);
    auto const n=state->response_api->drain_events(state->world,events.data(),
        static_cast<std::uint32_t>(events.size()));
    if(n>events.size())throw state->module.error("Physics response event count invalid");
    std::vector<PhysicsContactEvent> result;
    result.reserve(n);
    for(std::uint32_t i=0;i<n;++i)
        result.push_back({events[i].first_index,events[i].second_index,
                          events[i].phase,events[i].response});
    return result;
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
    if (object.name.empty() || object.name.size() >= NORTHSTARCPP_ECS_NAME_BYTES ||
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
    if (!object.node.collision && !state->collider_api)
        throw std::runtime_error("Noncolliding runtime visuals require collider provider ABI");
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
    if (!object.node.collision) {
        NorthStarPhysicsCollider const disabled{0u,0u,object.size,object.size,
            object.size,0.f,0.f,0.f};
        if(!state->collider_api->configure(state->world,result,&disabled)) {
            (void)state->runtime_api->remove_body(state->world,result);
            state->snapshot.objects.pop_back();
            state->poses.pop_back();
            state->resident_mask.pop_back();
            state->runtime_alive.pop_back();
            throw state->module.error("Unable to disable runtime visual collision");
        }
    }
    state->used_ids.insert(object.node.id);
    // Initialize only the new pose instead of reading every provider body.
    // PhysicsScene::step updates all dynamic poses normally on the next tick.
    state->poses[index] = {object.position.x, object.position.y, object.position.z,
        {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f}};
    return index;
}

void PhysicsScene::rotate_authored_visual(
    std::size_t index,Vector3 euler_degrees) {
    if(index>=state->authored_count)
        throw std::out_of_range("Authored visual index required");
    auto& object=state->snapshot.objects[index];
    if(object.mass!=0.f || object.node.collision || !object.node.id)
        throw std::invalid_argument("Authored visual must be static without collision");
    bool const skeletal=std::any_of(state->snapshot.components.begin(),
        state->snapshot.components.end(),[&](SceneComponentBlob const& component) {
            return component.entity_id==object.node.id &&
                   component.type=="northstar.gameplay.skeletal_mesh";
        });
    if(!skeletal)
        throw std::invalid_argument("Authored visual needs SkeletalMesh component");
    if(!std::isfinite(euler_degrees.x)||!std::isfinite(euler_degrees.y)||
       !std::isfinite(euler_degrees.z)||
       std::max({std::abs(euler_degrees.x),std::abs(euler_degrees.y),
                 std::abs(euler_degrees.z)})>360000.f)
        throw std::invalid_argument("Invalid authored visual rotation");
    object.node.rotation=euler_degrees;
    state->authored_snapshot.objects[index].node.rotation=euler_degrees;
}
void PhysicsScene::rotate_runtime(std::size_t index, Vector3 euler_degrees) {
    // The Editor/Project API uses the same rotation entry point for the
    // authored visual; reject nonvisual authored actors in the guard below.
    if(index<state->authored_count) {
        rotate_authored_visual(index,euler_degrees);
        return;
    }
    if(!runtime_alive(index) || state->snapshot.objects[index].mass!=0.f ||
       state->snapshot.objects[index].node.collision)
        throw std::invalid_argument("Rotation requires a live visual-only runtime entity");
    if(!std::isfinite(euler_degrees.x) || !std::isfinite(euler_degrees.y) ||
       !std::isfinite(euler_degrees.z) ||
       std::max({std::abs(euler_degrees.x),std::abs(euler_degrees.y),
                 std::abs(euler_degrees.z)})>360000.f)
        throw std::invalid_argument("Invalid visual rotation");
    state->snapshot.objects[index].node.rotation=euler_degrees;
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
    std::vector<NorthStarPhysicsVehiclePose> vehicle_guards;
    for(auto id:state->native_vehicles) {
        NorthStarPhysicsVehiclePose pose{};pose.size=sizeof(pose);
        if(!state->vehicle_api || !state->vehicle_api->pose(state->world,id,&pose))
            throw state->module.error("Vehicle streaming pose unavailable");
        vehicle_guards.push_back(pose);
    }
    std::size_t changed=0;
    for (std::size_t i=0;i<authored.size();++i) {
        auto const& object=objects[i];
        auto const& node=authored[i].node;
        if (node.id!=object.node.id)
            throw std::runtime_error("Collision streaming stable ID mismatch");
        // Never detach dynamic traffic/NPCs or their player controller.
        if (scene_pawn::is_pawn(state->snapshot,object) || object.mass>0.f ||
            state->native_vehicles.count(static_cast<std::uint32_t>(i)))
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
        bool vehicle_nearby=false;
        for(auto const& v:vehicle_guards) {
            float const ahead_x=v.x+v.linear_velocity[0]*2.f;
            float const ahead_z=v.z+v.linear_velocity[2]*2.f;
            float const radius=std::clamp(48.f+2.f*std::hypot(
                v.linear_velocity[0],v.linear_velocity[2]),48.f,180.f)+object.size;
            float const dx=object.position.x-ahead_x;
            float const dz=object.position.z-ahead_z;
            float const dy=object.position.y-v.y;
            if(dx*dx+dy*dy+dz*dz<=radius*radius) {
                vehicle_nearby=true;break;
            }
        }
        bool const desired=node.collision &&
            (nearby || vehicle_nearby || scene_world.object_resident(i));
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
    if (index>=objects.size() || objects[index].mass<=0.f || scene_pawn::is_pawn(state->snapshot,objects[index]) ||
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
    NorthStarPhysicsSurfaceHit surface_hit{};
    surface_hit.triangle_index=UINT32_MAX;
    if (state->surface_api) {
        if(!state->surface_api->sweep_sphere_surface(
            state->world,start.x,start.y,start.z,
            end.x,end.y,end.z,radius,ignored,&surface_hit))
            throw state->module.error("Physics surface sweep failed");
        hit=surface_hit.cast;
    } else if (!state->api.sweep_sphere ||
        !state->api.sweep_sphere(state->world,start.x,start.y,start.z,
            end.x,end.y,end.z,radius,ignored,&hit))
        throw state->module.error("Physics sphere sweep failed");
    if (!hit.hit) return std::nullopt;
    if (!std::isfinite(hit.fraction) || hit.fraction<0.f ||
        hit.fraction>1.f || !std::isfinite(hit.normal_x) ||
        !std::isfinite(hit.normal_y) || !std::isfinite(hit.normal_z) ||
        surface_hit.material_id>physics_surface::maximum)
        throw std::runtime_error("Physics provider returned invalid sweep hit");
    return PhysicsCastHit{hit.fraction,
        {hit.normal_x,hit.normal_y,hit.normal_z},hit.object_index,
        surface_hit.material_id,surface_hit.triangle_index};
}

void PhysicsScene::add_static_mesh(std::vector<Vector3> const& vertices,
                                  std::vector<std::uint32_t> const& triangles) {
    add_static_mesh(vertices,triangles,{});
}
#include "physics_channel_queries.inc"
bool PhysicsScene::supports_surface_materials() const {
    return state->surface_api!=nullptr;
}
void PhysicsScene::add_static_mesh(std::vector<Vector3> const& vertices,
                                  std::vector<std::uint32_t> const& triangles,
                                  std::vector<std::uint32_t> const& material_ids)
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
    if(!material_ids.empty()) {
        if(material_ids.size()!=triangles.size()/3 || !state->surface_api)
            throw std::invalid_argument("Triangle materials require the physics surface ABI");
        for(auto id:material_ids)
            if(id>physics_surface::maximum)
                throw std::invalid_argument("Unknown physics material ID");
        if(!state->surface_api->add_static_mesh_materials(state->world,
            flat.data(),static_cast<std::uint32_t>(vertices.size()),
            triangles.data(),static_cast<std::uint32_t>(triangles.size()),
            material_ids.data(),static_cast<std::uint32_t>(material_ids.size())))
            throw state->module.error("Physics material mesh registration failed");
        return;
    }
    if (!state->api.add_static_mesh ||
        !state->api.add_static_mesh(state->world,flat.data(),
            static_cast<std::uint32_t>(vertices.size()),triangles.data(),
            static_cast<std::uint32_t>(triangles.size())))
        throw state->module.error("Physics static triangle registration failed");
}

void PhysicsScene::possess(std::uint64_t entity_id) {
    if(!state->character_api || !state->character_api->possess_body ||
       !state->character_api->configure_motor)
        throw std::runtime_error("Physics provider lacks Pawn control extension");
    gameplay::PlayerController proposed=state->controller;
    proposed.possess(state->snapshot,entity_id);
    auto const index=proposed.controlled_index(state->snapshot);
    if(!index || *index>UINT32_MAX)
        throw std::out_of_range("Controlled entity has no provider body");
    auto const tune=gameplay::motor_settings(state->snapshot,entity_id);
    NorthStarPhysicsCharacterMotor motor{tune.walk_speed,tune.run_speed,
        tune.jump_speed,tune.acceleration,tune.air_control,tune.gravity_scale};
    if(!state->character_api->configure_motor(state->world,std::uint32_t(*index),&motor) ||
       !state->character_api->possess_body(state->world,std::uint32_t(*index)))
        throw state->module.error("Cannot possess physics Pawn");
    state->controller=proposed;
    state->initialize_player_animation();
}
void PhysicsScene::unpossess() {
    if(!state->character_api || !state->character_api->possess_body ||
       !state->character_api->possess_body(state->world,UINT32_MAX))
        throw state->module.error("Cannot unpossess physics Pawn");
    state->controller.unpossess();
    state->initialize_player_animation();
}
std::uint64_t PhysicsScene::controlled_entity() const noexcept {
    return state->controller.controlled_entity();
}
std::optional<std::size_t> PhysicsScene::controlled_index() const noexcept {
    return state->controller.controlled_index(state->snapshot);
}
void PhysicsScene::set_sprinting(bool active) {
    if(!state->character_api || !state->character_api->set_sprinting ||
       !state->character_api->set_sprinting(state->world,active?1u:0u))
        throw state->module.error("Physics provider lacks Character sprint control");
}
std::optional<PlayerAnimationFrame> PhysicsScene::player_animation() const {
    if(!state->player_animator || !state->animation_pawn_id)
        return std::nullopt;
    auto const& sample=state->player_animator->sample();
    return PlayerAnimationFrame{
        state->animation_pawn_id,state->animation_mesh_id,
        player_animation::name(sample.state),sample.clip,
        sample.previous_clip,sample.blend_alpha,sample.playback_seconds,
        sample.grounded};
}

void PhysicsScene::step(float seconds, float input_x, float input_z)
{
    if (!std::isfinite(seconds) || seconds <= 0.f)
        return;
    auto const controlled=state->controller.controlled_index(state->snapshot);
    std::optional<Vector3> previous;
    if(controlled)previous=state->snapshot.objects[*controlled].position;
    if (!state->api.step(state->world, seconds, input_x, input_z))
        throw state->module.error("Cannot update physics world");
    state->read_poses();
    if(controlled && previous && state->player_animator) {
        auto const& position=state->snapshot.objects[*controlled].position;
        float const dx=position.x-previous->x,dz=position.z-previous->z;
        float const speed=std::sqrt(dx*dx+dz*dz)/seconds;
        float const rise=(position.y-previous->y)/seconds;
        bool grounded=std::abs(position.y)<.13f;
        auto hit=sweep_sphere({position.x,position.y+.22f,position.z},
                              {position.x,position.y-.35f,position.z},
                              .10f,*controlled);
        grounded |= hit && hit->normal.y>=.55f;
        state->player_animator->advance(std::min(seconds,.25f),speed,
                                        grounded,rise>.25f);
    }
    state->ecs.sync_positions(state->authored_snapshot);
    state->ecs.step(std::min(seconds, 0.1f), 0.f, 0.f);
}

bool PhysicsScene::supports_native_vehicle() const {
    return state->vehicle_api!=nullptr;
}
void PhysicsScene::create_native_vehicle(std::size_t index,
    NorthStarVehicleRig const& rig,float yaw) {
    if(!state->vehicle_api)
        throw std::runtime_error("Selected engine.physics provider lacks Jolt native vehicle extension");
    if(index>=state->snapshot.objects.size() ||
       !state->snapshot.objects[index].node.collision ||
       scene_pawn::is_pawn(state->snapshot,state->snapshot.objects[index]) ||
       state->snapshot.objects[index].mass!=0.f)
        throw std::invalid_argument("Native vehicle must own a collidable, static authored entity");
    if(!state->vehicle_api->create(state->world,
        static_cast<std::uint32_t>(index),&rig,yaw))
        throw state->module.error("Jolt cannot construct native vehicle");
    state->resident_mask.at(index)=1;
    state->native_vehicles.insert(static_cast<std::uint32_t>(index));
}
void PhysicsScene::destroy_native_vehicle(std::size_t index) {
    if(!state->vehicle_api || index>=state->snapshot.objects.size() ||
       !state->vehicle_api->destroy(state->world,
        static_cast<std::uint32_t>(index)))
        throw state->module.error("Cannot destroy Jolt native vehicle");
    state->native_vehicles.erase(static_cast<std::uint32_t>(index));
}
void PhysicsScene::native_vehicle_controls(std::size_t index,float throttle,
    float steering,float brake,float handbrake) {
    if(!state->vehicle_api ||
       !state->vehicle_api->controls(state->world,
            static_cast<std::uint32_t>(index),throttle,steering,brake,handbrake))
        throw state->module.error("Native Jolt vehicle controls failed");
}
NorthStarPhysicsVehiclePose PhysicsScene::native_vehicle_pose(std::size_t index) const {
    NorthStarPhysicsVehiclePose out{};out.size=sizeof(out);
    if(!state->vehicle_api ||
       !state->vehicle_api->pose(state->world,
            static_cast<std::uint32_t>(index),&out) ||
       out.size<sizeof(out))
        throw state->module.error("Native Jolt vehicle pose unavailable");
    return out;
}
NorthStarVehicleWheelState PhysicsScene::native_vehicle_wheel(std::size_t index,
    std::uint32_t wheel) const {
    NorthStarVehicleWheelState out{};out.size=sizeof(out);
    if(!state->vehicle_api ||
       !state->vehicle_api->wheel(state->world,
            static_cast<std::uint32_t>(index),wheel,&out) ||
       out.size<sizeof(out))
        throw state->module.error("Native Jolt wheel telemetry unavailable");
    return out;
}
std::uint32_t PhysicsScene::native_vehicle_wheel_count(std::size_t index) const {
    return state->vehicle_api?
        state->vehicle_api->wheel_count(state->world,static_cast<std::uint32_t>(index)):0u;
}

} // namespace northstarcpp
