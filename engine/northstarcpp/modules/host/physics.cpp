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
#include <northstarcpp/physics_terrain_streaming_api.h>
#include <northstarcpp/physics_surface.hpp>
#include "module_loader.hpp"
#include "physics_residency_guards.hpp"
#include "character_motor_provider_bridge.hpp"
// Collision authoring contract is provided by <northstarcpp/collision_authoring.hpp>.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <mutex>

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
        body_material_api=module.query_extension<NorthStarPhysicsBodyMaterialApi>(
            NORTHSTARCPP_PHYSICS_BODY_MATERIAL_MODULE_ABI);
        response_api=module.query_extension<NorthStarPhysicsResponseApi>(
            NORTHSTARCPP_PHYSICS_RESPONSE_MODULE_ABI);
        channels_api=module.query_extension<NorthStarPhysicsChannelsApi>(
            NORTHSTARCPP_PHYSICS_CHANNELS_MODULE_ABI);
        character_api = module.query_extension<NorthStarPhysicsCharacterApi>(
            NORTHSTARCPP_PHYSICS_CHARACTER_MODULE_ABI);
        ai_motor_api=module.query_extension<NorthStarPhysicsAiMotorApi>(
            NORTHSTARCPP_PHYSICS_AI_MOTOR_MODULE_ABI);
        root_motion_api=module.query_extension<NorthStarPhysicsRootMotionApi>(
            NORTHSTARCPP_PHYSICS_ROOT_MOTION_MODULE_ABI);
        terrain_api=module.query_extension<NorthStarPhysicsTerrainApi>(
            NORTHSTARCPP_PHYSICS_TERRAIN_MODULE_ABI);
        if(!terrain_api){
            auto const query=module.symbol<NorthStarPhysicsGetTerrainApi>(
                "northstar_physics_get_terrain_api");
            auto const* candidate=query?query(NORTHSTARCPP_PHYSICS_TERRAIN_ABI):nullptr;
            if(candidate && candidate->abi_version==NORTHSTARCPP_PHYSICS_TERRAIN_ABI &&
               candidate->upsert && candidate->remove && candidate->resident_count && candidate->commit)
                terrain_api=candidate;
        }
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
                if(!character_api || !character_api->set_sprinting)
                    throw std::runtime_error("Selected physics provider lacks Pawn API");
                auto const index=controller.controlled_index(snapshot);
                if(!index || *index>UINT32_MAX)
                    throw std::runtime_error("Pawn is absent from physics world");
                auto const tune=gameplay::motor_settings(snapshot,controller.controlled_entity());
                NorthStarPhysicsCharacterMotor motor{tune.walk_speed,tune.run_speed,
                    tune.jump_speed,tune.acceleration,tune.air_control,tune.gravity_scale};
                auto const outcome=gameplay::physics_bridge::transfer(
                    character_api,ai_motor_api,world,std::uint32_t(*index),motor,false);
                if(outcome!=gameplay::physics_bridge::Result::Ok)
                    throw module.error(gameplay::physics_bridge::message(outcome));
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
    NorthStarPhysicsBodyMaterialApi const* body_material_api = nullptr;
    NorthStarPhysicsResponseApi const* response_api = nullptr;
    NorthStarPhysicsChannelsApi const* channels_api = nullptr;
    NorthStarPhysicsSurfaceApi const* surface_api = nullptr;
    NorthStarPhysicsTerrainApi const* terrain_api = nullptr;
    NorthStarPhysicsVehicleApi const* vehicle_api = nullptr;
    NorthStarPhysicsCharacterApi const* character_api = nullptr;
    NorthStarPhysicsRootMotionApi const* root_motion_api = nullptr;
    NorthStarPhysicsAiMotorApi const* ai_motor_api = nullptr;
    gameplay::PlayerController controller;
    std::optional<player_animation::Controller> player_animator;
    std::uint64_t animation_pawn_id=0;
    std::uint64_t animation_mesh_id=0;
    std::unordered_set<std::uint32_t> native_vehicles;
    std::vector<uint8_t> resident_mask;
    // Persistent per-world scratch: no heap churn during physics streaming/contact polling.
    std::vector<physics_detail::VehicleResidencyGuard> vehicle_residency_guards;
    std::vector<NorthStarPhysicsContactEvent> contact_event_scratch;
    struct DeferredGroundProbe {
        std::uint64_t ticket;
        Vector3 start,end;
        std::uint32_t channel;
        std::size_t ignore;
    };
    mutable std::mutex root_motion_mutex;
    mutable Vector3 queued_root_motion{0.f,0.f,0.f};
    mutable float queued_root_seconds=0.f;
    mutable std::mutex ground_probe_mutex;
    std::deque<DeferredGroundProbe> ground_probe_pending;
    std::deque<std::pair<std::uint64_t,PhysicsGroundProbe>> ground_probe_completed;
    std::uint64_t next_probe_ticket=1;
    bool warned_ground_probe_provider=false;
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

std::optional<math::float4x4> PhysicsScene::body_transform(
    std::size_t index) const {
    if(index>=state->poses.size() ||index>=state->snapshot.objects.size())
        return std::nullopt;
    if(index>=state->authored_count &&
       (index>=state->runtime_alive.size()||!state->runtime_alive[index]))
        return std::nullopt;
    auto pose=orientation(index);
    auto const& position=state->snapshot.objects[index].position;
    if(!std::isfinite(position.x)||!std::isfinite(position.y)||
       !std::isfinite(position.z))return std::nullopt;
    pose.elements[3][0]=position.x;
    pose.elements[3][1]=position.y;
    pose.elements[3][2]=position.z;
    return pose;
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
    auto& scratch=state->contact_event_scratch;
    if(scratch.size()<max_events)
        scratch.resize(max_events);
    auto const n=state->response_api->drain_events(state->world,scratch.data(),
        static_cast<std::uint32_t>(max_events));
    if(n>max_events)throw state->module.error("Physics response event count invalid");
    std::vector<PhysicsContactEvent> result;
    result.reserve(n);
    for(std::uint32_t i=0;i<n;++i)
        result.push_back({scratch[i].first_index,scratch[i].second_index,
                          scratch[i].phase,scratch[i].response});
    return result;
}

bool PhysicsScene::set_body_material(
    std::size_t index,float friction,float restitution) {
    if(!std::isfinite(friction)||!std::isfinite(restitution)||
       friction<0.f||friction>1.f||restitution<0.f||restitution>1.f||
       index>=state->snapshot.objects.size() ||
       state->snapshot.objects[index].mass<=0.f ||
       (index>=state->authored_count && !runtime_alive(index)))
        throw std::invalid_argument("Invalid physics body material");
    if(!state->body_material_api)return false;
    if(!state->body_material_api->configure(state->world,
        static_cast<std::uint32_t>(index),friction,restitution))
        throw state->module.error("Cannot configure dynamic body material");
    return true;
}
std::size_t PhysicsScene::authored_object_count() const { return state->authored_count; }
Scene const& PhysicsScene::authored_scene() const { return state->authored_snapshot; }
bool PhysicsScene::runtime_alive(std::size_t index) const
{
    return index >= state->authored_count &&
        index - state->authored_count < state->runtime_alive.size() &&
        state->runtime_alive[index - state->authored_count] != 0;
}

#include "physics_runtime_entities.inc"

std::size_t PhysicsScene::ecs_resident_entity_count() const
{
    return state->ecs.size();
}

#include "physics_scene_residency.inc"

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
void PhysicsScene::commit_streamed_terrain() {
    if(!state->terrain_api)
        throw std::runtime_error("Physics provider lacks streamed terrain extension");
    if(!state->terrain_api->commit(state->world))
        throw state->module.error("Physics provider terrain commit failed");
}
bool PhysicsScene::supports_streamed_terrain() const noexcept {
    return state->terrain_api!=nullptr;
}
std::uint32_t PhysicsScene::terrain_resident_count() const noexcept {
    return state->terrain_api ? state->terrain_api->resident_count(state->world) : 0u;
}
void PhysicsScene::upsert_terrain_chunk(std::int32_t cx,std::int32_t cz,
    std::vector<Vector3> const& vertices,
    std::vector<std::uint32_t> const& indices) {
    if(!state->terrain_api)
        throw std::runtime_error("Physics provider lacks streamed terrain extension");
    if(vertices.size()<4||vertices.size()>16384||indices.size()<6||
       indices.size()>98304||indices.size()%3)
        throw std::invalid_argument("Invalid streamed terrain mesh size");
    std::vector<float> xyz;
    xyz.reserve(vertices.size()*3);
    for(auto const& v:vertices){
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
            throw std::invalid_argument("Invalid terrain vertex");
        xyz.insert(xyz.end(),{v.x,v.y,v.z});
    }
    for(auto const idx:indices)
        if(idx>=vertices.size())
            throw std::invalid_argument("Invalid terrain triangle index");
    if(!state->terrain_api->upsert(state->world,cx,cz,xyz.data(),
                                  std::uint32_t(vertices.size()),
                                  indices.data(),std::uint32_t(indices.size())))
        throw state->module.error("Physics provider terrain registration failed");
}
void PhysicsScene::remove_terrain_chunk(std::int32_t cx,std::int32_t cz) {
    if(!state->terrain_api)
        throw std::runtime_error("Physics provider lacks streamed terrain extension");
    if(!state->terrain_api->remove(state->world,cx,cz))
        throw state->module.error("Physics provider terrain eviction failed");
}

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

#include "physics_player_possession.inc"
#include "physics_player_root_motion.inc"
#include "physics_ai_motor_bridge.inc"

std::uint64_t PhysicsScene::enqueue_ground_probe(Vector3 start,Vector3 end,
    std::uint32_t channel,std::size_t ignore_index) const {
    auto finite=[](Vector3 const& p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
            std::abs(p.x)<1000000.f&&std::abs(p.y)<1000000.f&&
            std::abs(p.z)<1000000.f;
    };
    if(!finite(start)||!finite(end)||start.y<=end.y ||
       start.y-end.y>5.f||channel>=16 ||
       (ignore_index!=std::size_t(-1) &&
        ignore_index>UINT32_MAX))
        return 0;
    std::lock_guard lock(state->ground_probe_mutex);
    if(state->ground_probe_pending.size()>=96)return 0;
    auto ticket=state->next_probe_ticket++;
    if(ticket==0)ticket=state->next_probe_ticket++;
    state->ground_probe_pending.push_back(
        {ticket,start,end,channel,ignore_index});
    return ticket;
}

std::optional<PhysicsGroundProbe> PhysicsScene::consume_ground_probe(
    std::uint64_t ticket) const {
    if(ticket==0)return std::nullopt;
    std::lock_guard lock(state->ground_probe_mutex);
    auto& results=state->ground_probe_completed;
    auto it=std::find_if(results.begin(),results.end(),
        [ticket](auto const& result){return result.first==ticket;});
    if(it==results.end())return std::nullopt;
    auto output=it->second;
    results.erase(it);
    return output;
}

void PhysicsScene::step(float seconds, float input_x, float input_z)
{
    if (!std::isfinite(seconds) || seconds <= 0.f)
        return;
    auto const controlled=state->controller.controlled_index(state->snapshot);
    std::optional<Vector3> previous;
    if(controlled)previous=state->snapshot.objects[*controlled].position;
    Vector3 root_delta{0.f,0.f,0.f};
    float root_seconds=0.f;
    if(state->root_motion_api &&controlled &&*controlled<=UINT32_MAX) {
        std::lock_guard<std::mutex> lock(state->root_motion_mutex);
        root_delta=state->queued_root_motion;
        root_seconds=state->queued_root_seconds;
        state->queued_root_motion={0.f,0.f,0.f};
        state->queued_root_seconds=0.f;
    }
    if(root_seconds>0.f &&
       !state->root_motion_api->submit(state->world,
           static_cast<std::uint32_t>(*controlled),
           root_delta.x,root_delta.y,root_delta.z,root_seconds))
        throw state->module.error("Physics provider refused Root Motion");
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

    // Queue from animation; execute only after the Jolt/Bullet simulation
    // step. Worker threads never access a mutable provider world.
    std::deque<State::DeferredGroundProbe> jobs;
    {
        std::lock_guard lock(state->ground_probe_mutex);
        jobs.swap(state->ground_probe_pending);
    }
    std::deque<std::pair<std::uint64_t,PhysicsGroundProbe>> completed;
    for(auto const& job:jobs) {
        PhysicsGroundProbe output;
        try {
            auto const hit=state->channels_api?
                raycast_channel(job.start,job.end,job.channel,false,job.ignore):
                sweep_sphere(job.start,job.end,.025f,job.ignore);
            if(hit && hit->normal.y>=.20f && hit->response==2u) {
                output.hit=true;
                output.normal=hit->normal;
                output.fraction=hit->fraction;
                output.object_index=hit->object_index;
                output.point=job.start+(job.end-job.start)*hit->fraction;
                if(!state->channels_api)
                    output.point-=output.normal*.025f;
            }
        } catch(std::exception const&) {
            if(!state->warned_ground_probe_provider) {
                state->warned_ground_probe_provider=true;
                std::fprintf(stderr,
                    "[WARN] [engine.ik] Ground probe failed: ignoring unavailable contact\n");
            }
        }
        completed.emplace_back(job.ticket,output);
    }
    {
        std::lock_guard lock(state->ground_probe_mutex);
        for(auto& item:completed) {
            if(state->ground_probe_completed.size()>=192)
                state->ground_probe_completed.pop_front();
            state->ground_probe_completed.push_back(std::move(item));
        }
    }
}

#include "physics_vehicle_bindings.inc"

} // namespace northstarcpp
