// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
// Alternative physics provider: Jolt Physics by Jorrit Rouwe (MIT).
// Jolt upstream copyrights and license are retained in sdk/vendor/sources/JoltPhysics.

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#include <northstarcpp/physics_plugin.h>
#include <northstarcpp/physics_vehicle_plugin.h>
#include "module_provider.hpp"
#include "jolt_mesh.hpp"
#include "jolt_response_layers.hpp"
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <map>
#include <unordered_map>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace northstarcpp::module_provider;
namespace j = JPH;

constexpr float player_speed = 4.f;
constexpr double fixed_step = 1.0 / 120.0;
constexpr double max_frame_time = 0.1;

namespace layers {
constexpr j::ObjectLayer static_body = northstarcpp::jolt_collision::static_default;
constexpr j::ObjectLayer dynamic_body = northstarcpp::jolt_collision::dynamic_default;
}
namespace broadphase {
const j::BroadPhaseLayer static_body(0);
const j::BroadPhaseLayer dynamic_body(1);
}

class BroadPhaseLayers final : public j::BroadPhaseLayerInterface {
public:
    j::uint GetNumBroadPhaseLayers() const override { return 2; }
    j::BroadPhaseLayer GetBroadPhaseLayer(j::ObjectLayer layer) const override
    {
        return northstarcpp::jolt_collision::dynamic(layer)?broadphase::dynamic_body:broadphase::static_body;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(j::BroadPhaseLayer layer) const override
    {
        return layer == broadphase::static_body ? "static" : "dynamic";
    }
#endif
};

class LayerPairs final : public j::ObjectLayerPairFilter {
    northstarcpp::jolt_collision::Registry const& registry_;
public:
    explicit LayerPairs(northstarcpp::jolt_collision::Registry const& registry)
        :registry_(registry){}
    bool ShouldCollide(j::ObjectLayer a,j::ObjectLayer b) const override {
        return registry_.pair_allowed(a,b);
    }
};

class LayerVsBroadPhase final : public j::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(j::ObjectLayer a, j::BroadPhaseLayer b) const override
    {
        return northstarcpp::jolt_collision::dynamic(a) || b == broadphase::dynamic_body;
    }
};

// Registered once for the lifetime of this loaded DLL. All world instances must
// be destroyed by the host before it unloads this module.
struct Library {
    Library()
    {
        j::RegisterDefaultAllocator();
        if (j::Factory::sInstance)
            throw std::runtime_error("Jolt factory is already registered");
        j::Factory::sInstance = new j::Factory();
        j::RegisterTypes();
    }

    ~Library()
    {
        j::UnregisterTypes();
        delete j::Factory::sInstance;
        j::Factory::sInstance = nullptr;
    }
};
Library& library() { static Library instance; return instance; }

float sanitized_input(float n)
{
    return std::isfinite(n) ? std::clamp(n, -1.0f, 1.0f) : 0.f;
}

struct Body {
    j::BodyID id;
    float half = 0.f;
    bool is_static = false;
    bool resident = true;
    bool alive = true;
    bool collision_enabled = true;
    std::uint32_t object_type=0;
    std::uint32_t triangle_count=0;
    std::vector<std::uint32_t> triangle_materials;
};

struct World {
    explicit World(NorthStarRigidBody const* objects, uint32_t count)
        : initialization(library()),
          layer_pairs(registry),contact_events(registry),
          jobs(j::cMaxPhysicsJobs, j::cMaxPhysicsBarriers,
               std::max(1u, std::min(8u, std::thread::hardware_concurrency()))),
          temporary_allocator(8 * 1024 * 1024)
    {
        if (count && !objects)
            throw std::runtime_error("Missing rigid bodies");
        // A bounded budget prevents an accidental allocation explosion.
        if (count > 100000)
            throw std::runtime_error("Too many physics objects for Jolt world");
        // Keep spare capacity for incremental script-spawned bodies.
        auto const max_bodies = std::max(2048u, count + 4096u);
        physics.Init(max_bodies, 0, std::max(8192u, 8u * max_bodies),
            std::max(4096u, 4u * max_bodies), broad_phase, layer_vs_broadphase, layer_pairs);
        physics.SetGravity(j::Vec3(0.f, -9.81f, 0.f));
        physics.SetContactListener(&contact_events);
        auto& iface = physics.GetBodyInterface();

        // Matches Bullet's implicit infinite ground (top of the plane is at y=0).
        j::RefConst<j::Shape> ground_shape = new j::PlaneShape(
            j::Plane(j::Vec3::sAxisY(), 0.f), nullptr, 100000.f);
        j::BodyCreationSettings floor(ground_shape, j::RVec3::sZero(),
            j::Quat::sIdentity(), j::EMotionType::Static, layers::static_body);
        ground = iface.CreateAndAddBody(floor, j::EActivation::DontActivate);
        if (ground.IsInvalid())
            throw std::runtime_error("Jolt ground body creation failed");

        initial_body_count = count;
        bodies.reserve(count);
        for (uint32_t n = 0; n < count; ++n) {
            auto const& input = objects[n];
            if (!std::isfinite(input.x) || !std::isfinite(input.y) ||
                !std::isfinite(input.z) || !std::isfinite(input.size) ||
                !std::isfinite(input.mass) || input.size <= 0.f || input.mass < 0.f)
                throw std::runtime_error("Invalid rigid body parameters");

            float const half = input.size * 0.5f;
            j::RefConst<j::Shape> shape = new j::BoxShape(j::Vec3(half, half, half));
            j::BodyCreationSettings setup(shape, j::RVec3(input.x, input.y + half, input.z),
                j::Quat::sIdentity(),
                input.mass > 0.f ? j::EMotionType::Dynamic : j::EMotionType::Static,
                input.mass > 0.f ? layers::dynamic_body : layers::static_body);
            setup.mFriction = input.initially_controlled ? 0.f : 0.65f;
            setup.mUserData=std::uint64_t(n)+1;
            if (input.mass > 0.f) {
                setup.mMotionQuality = j::EMotionQuality::LinearCast;
                setup.mOverrideMassProperties = j::EOverrideMassProperties::CalculateInertia;
                setup.mMassPropertiesOverride.mMass = input.mass;
            }
            if (input.initially_controlled) {
                setup.mAllowedDOFs = j::EAllowedDOFs::TranslationX |
                    j::EAllowedDOFs::TranslationY | j::EAllowedDOFs::TranslationZ;
            }
            j::BodyID id = iface.CreateAndAddBody(setup, input.mass > 0.f ?
                j::EActivation::Activate : j::EActivation::DontActivate);
            if (id.IsInvalid())
                throw std::runtime_error("Jolt rigid body creation failed");
            bodies.push_back({id, half, input.mass == 0.f});
            if (input.initially_controlled) {
                player = id;
                controlled_index = n;
            }
        }
        physics.OptimizeBroadPhase();
    }

    ~World()
    {
        for(auto& entry:vehicles) {
            physics.RemoveStepListener(entry.second.constraint);
            physics.RemoveConstraint(entry.second.constraint);
        }
        vehicles.clear();
        auto& iface = physics.GetBodyInterface();
        for (auto const& b : bodies) {
            if (!b.alive) continue;
            if (b.resident)
                iface.RemoveBody(b.id);
            iface.DestroyBody(b.id);
        }
        for (auto const id : static_meshes) {
            iface.RemoveBody(id);
            iface.DestroyBody(id);
        }
        if (!ground.IsInvalid()) {
            iface.RemoveBody(ground);
            iface.DestroyBody(ground);
        }
    }

    void possess_body(std::uint32_t index) {
        if(index!=UINT32_MAX &&
           (index>=bodies.size() || !bodies[index].alive ||
            !bodies[index].resident || bodies[index].is_static ||
            vehicles.count(index)))
            throw std::invalid_argument("Invalid Jolt Pawn possession target");
        auto& iface=physics.GetBodyInterface();
        if(!player.IsInvalid()) {
            auto const velocity=iface.GetLinearVelocity(player);
            iface.SetLinearVelocity(player,j::Vec3(0.f,velocity.GetY(),0.f));
            iface.SetGravityFactor(player,1.f);
        }
        player=index==UINT32_MAX?j::BodyID():bodies[index].id;
        controlled_index=index;
        sprinting=false;
        if(!player.IsInvalid()) {
            auto const cfg=motors.find(index);
            iface.SetGravityFactor(player,cfg==motors.end()?1.f:
                                   cfg->second.gravity_scale);
            iface.SetAngularVelocity(player,j::Vec3::sZero());
        }
    }
    void configure_motor(std::uint32_t index,
                         NorthStarPhysicsCharacterMotor const& cfg) {
        if(index>=bodies.size() || !bodies[index].alive ||
           bodies[index].is_static ||
           !std::isfinite(cfg.walk_speed) || cfg.walk_speed<=0.f ||
           cfg.walk_speed>200.f || !std::isfinite(cfg.run_speed) ||
           cfg.run_speed<cfg.walk_speed || cfg.run_speed>200.f ||
           !std::isfinite(cfg.jump_speed) || cfg.jump_speed<0.f ||
           cfg.jump_speed>100.f || !std::isfinite(cfg.acceleration) ||
           cfg.acceleration<0.f || cfg.acceleration>10000.f ||
           !std::isfinite(cfg.air_control) || cfg.air_control<0.f ||
           cfg.air_control>1.f || !std::isfinite(cfg.gravity_scale) ||
           cfg.gravity_scale<0.f || cfg.gravity_scale>20.f)
            throw std::invalid_argument("Invalid Jolt CharacterMotor parameters");
        motors[index]=cfg;
        if(index==controlled_index && !player.IsInvalid())
            physics.GetBodyInterface().SetGravityFactor(player,cfg.gravity_scale);
    }
    void set_sprinting(bool enabled) noexcept {sprinting=enabled;}

    void step(float delta, float input_x, float input_z)
    {
        if (!std::isfinite(delta) || delta <= 0.f)
            return;

        float x = sanitized_input(input_x);
        float z = sanitized_input(input_z);
        auto const cfg=motors.find(controlled_index);
        float const desired_speed=(cfg==motors.end()?player_speed:
            sprinting?cfg->second.run_speed:cfg->second.walk_speed) /
            std::max(1.f,std::hypot(x,z));
        elapsed += std::min(static_cast<double>(delta), max_frame_time);
        auto& iface = physics.GetBodyInterface();
        while (elapsed + 1.0e-9 >= fixed_step) {
            if (!player.IsInvalid()) {
                if (x != 0.f || z != 0.f)
                    iface.ActivateBody(player);
                if (physics.GetBodyInterface().IsActive(player)) {
                    float vertical = iface.GetLinearVelocity(player).GetY();
                    auto velocity=iface.GetLinearVelocity(player);
                    float const accel=cfg==motors.end()?1.f:
                        std::clamp(cfg->second.acceleration*float(fixed_step)*
                            (std::abs(vertical)>.65f?cfg->second.air_control:1.f),
                            0.f,1.f);
                    iface.SetLinearVelocity(player,j::Vec3(
                        velocity.GetX()+(x*desired_speed-velocity.GetX())*accel,
                        vertical,
                        velocity.GetZ()+(z*desired_speed-velocity.GetZ())*accel));
                }
            }
            auto error = physics.Update(static_cast<float>(fixed_step), 1,
                &temporary_allocator, &jobs);
            if (error != j::EPhysicsUpdateError::None)
                throw std::runtime_error("Jolt Physics Update reported an error");
            elapsed -= fixed_step;
        }
    }

    bool grounded() const
    {
        if (player.IsInvalid()) return false;
        auto const& iface = physics.GetBodyInterface();
        auto velocity = iface.GetLinearVelocity(player);
        if (std::abs(velocity.GetY()) > 0.65f) return false;
        auto p = iface.GetPosition(player);
        float half = 0.75f;
        for (auto const& body : bodies)
            if (body.id == player) { half = body.half; break; }
        float const bottom = static_cast<float>(p.GetY()) - half;
        if (std::abs(bottom) < 0.13f) return true;
        for (auto const& body : bodies) {
            if (!body.resident || body.id == player || !body.is_static) continue;
            if (std::abs(bottom -
                (static_cast<float>(iface.GetPosition(body.id).GetY()) + body.half)) > 0.13f)
                continue;
            auto o = iface.GetPosition(body.id);
            if (std::abs(static_cast<float>(p.GetX() - o.GetX())) < half + body.half - 0.05f &&
                std::abs(static_cast<float>(p.GetZ() - o.GetZ())) < half + body.half - 0.05f)
                return true;
        }
        return false;
    }

    void jump()
    {
        if (!grounded()) return;
        auto& iface = physics.GetBodyInterface();
        auto v = iface.GetLinearVelocity(player);
        iface.ActivateBody(player);
        auto const cfg=motors.find(controlled_index);
        float const jump=cfg==motors.end()?5.35f:cfg->second.jump_speed;
        iface.SetLinearVelocity(player,j::Vec3(v.GetX(),jump,v.GetZ()));
    }

    void apply_impulse(uint32_t index, float x, float y, float z)
    {
        if(index>=bodies.size() || bodies[index].is_static ||
           bodies[index].id == player ||
           !std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z) ||
           std::hypot(x,y,z)>100.f)
            throw std::invalid_argument("Invalid Jolt dynamic-body impulse");
        physics.GetBodyInterface().AddImpulse(bodies[index].id,j::Vec3(x,y,z));
    }

    void move_static(uint32_t index, float x, float y, float z)
    {
        if (index >= bodies.size() || !bodies[index].alive ||
            !bodies[index].is_static ||
            !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            throw std::runtime_error("Invalid Jolt static relocation");
        auto& iface = physics.GetBodyInterface();
        iface.SetPosition(bodies[index].id, j::RVec3(x, y + bodies[index].half, z),
            j::EActivation::DontActivate);
    }

    uint32_t add_runtime_body(NorthStarRigidBody const& input)
    {
        if (bodies.size() >= 16384u || input.initially_controlled ||
            !std::isfinite(input.x) || !std::isfinite(input.y) ||
            !std::isfinite(input.z) || !std::isfinite(input.size) ||
            !std::isfinite(input.mass) || input.size <= 0.f ||
            input.size > 1000.f || input.mass < 0.f || input.mass > 1000000.f)
            throw std::invalid_argument("Invalid Jolt runtime body");
        auto const half = input.size * 0.5f;
        j::RefConst<j::Shape> shape = new j::BoxShape(j::Vec3(half, half, half));
        j::BodyCreationSettings setup(shape,
            j::RVec3(input.x, input.y + half, input.z), j::Quat::sIdentity(),
            input.mass > 0.f ? j::EMotionType::Dynamic : j::EMotionType::Static,
            input.mass > 0.f ? layers::dynamic_body : layers::static_body);
        setup.mFriction = 0.65f;
        setup.mUserData=std::uint64_t(bodies.size())+1;
        if (input.mass > 0.f) {
            setup.mMotionQuality = j::EMotionQuality::LinearCast;
            setup.mOverrideMassProperties = j::EOverrideMassProperties::CalculateInertia;
            setup.mMassPropertiesOverride.mMass = input.mass;
        }
        auto& iface = physics.GetBodyInterface();
        auto const id = iface.CreateAndAddBody(setup,
            input.mass > 0.f ? j::EActivation::Activate : j::EActivation::DontActivate);
        if (id.IsInvalid())
            throw std::runtime_error("Jolt runtime body allocation failed");
        try {
            bodies.push_back({id, half, input.mass == 0.f});
        } catch (...) {
            iface.RemoveBody(id);
            iface.DestroyBody(id);
            throw;
        }
        return static_cast<uint32_t>(bodies.size() - 1);
    }

    void remove_runtime_body(uint32_t index)
    {
        if (index < initial_body_count || index >= bodies.size() ||
            !bodies[index].alive)
            throw std::invalid_argument("Stale or authored Jolt body index");
        auto& entry = bodies[index];
        auto& iface = physics.GetBodyInterface();
        if (entry.resident) iface.RemoveBody(entry.id);
        iface.DestroyBody(entry.id);
        entry.id = j::BodyID();
        entry.resident = false;
        entry.alive = false;
    }

    void configure_collider(uint32_t index,NorthStarPhysicsCollider const& spec)
    {
        if(index>=bodies.size() || !bodies[index].alive ||
           spec.enabled>1u || spec.shape>2u)
            throw std::invalid_argument("Invalid Jolt collider target");
        for(float f:{spec.width,spec.height,spec.depth})
            if(!std::isfinite(f)||f<.01f||f>10000.f)
                throw std::invalid_argument("Invalid Jolt collider dimensions");
        for(float f:{spec.offset_x,spec.offset_y,spec.offset_z})
            if(!std::isfinite(f)||std::abs(f)>10000.f)
                throw std::invalid_argument("Invalid Jolt collider offset");
        if(spec.shape==2u && spec.height<spec.width)
            throw std::invalid_argument("Capsule total height is less than diameter");
        auto& body=bodies[index];
        auto& iface=physics.GetBodyInterface();
        if(body.resident) {
            iface.RemoveBody(body.id);
            body.resident=false;
        }
        j::RefConst<j::Shape> shape;
        if(spec.shape==1u || (spec.shape==2u && spec.height==spec.width))
            shape=new j::SphereShape(spec.width*.5f);
        else if(spec.shape==2u)
            shape=new j::CapsuleShape((spec.height-spec.width)*.5f,spec.width*.5f);
        else
            shape=new j::BoxShape(j::Vec3(spec.width*.5f,
                spec.height*.5f,spec.depth*.5f));
        // Body position still represents the original bottom-center anchor.
        auto const localY=(spec.shape==1u?spec.width:spec.height)*.5f-body.half+spec.offset_y;
        j::RefConst<j::Shape> translated=new j::RotatedTranslatedShape(
            j::Vec3(spec.offset_x,localY,spec.offset_z),j::Quat::sIdentity(),shape);
        iface.SetShape(body.id,translated,true,j::EActivation::DontActivate);
        body.collision_enabled=spec.enabled!=0;
        if(body.collision_enabled) {
            iface.AddBody(body.id,body.is_static?j::EActivation::DontActivate:
                                                 j::EActivation::Activate);
            body.resident=true;
        }
    }

    void configure_mesh(uint32_t index,uint32_t mode,uint32_t splits,
                        const float* vertices,uint32_t vertex_count,
                        const uint32_t* indices,uint32_t index_count) {
        if(index>=bodies.size() || !bodies[index].alive ||
           (mode!=3 && mode!=4) ||
           (mode==3 && !bodies[index].is_static))
            throw std::invalid_argument(
                "Jolt exact mesh requires a static actor; use Auto Convex for dynamic");
        auto shape=northstarcpp::jolt_mesh::build(
            vertices,vertex_count,indices,index_count,mode,splits);
        auto& body=bodies[index];
        auto& iface=physics.GetBodyInterface();
        bool const was_resident=body.resident;
        if(was_resident) {
            iface.RemoveBody(body.id);
            body.resident=false;
        }
        iface.SetShape(body.id,shape,true,j::EActivation::DontActivate);
        body.triangle_count=mode==3 ? index_count/3 : 0;
        body.triangle_materials.clear();
        if(was_resident && body.collision_enabled) {
            iface.AddBody(body.id,body.is_static?j::EActivation::DontActivate:
                                                   j::EActivation::Activate);
            body.resident=true;
        }
    }
    void set_triangle_materials(uint32_t index,
                                uint32_t const* ids,uint32_t count) {
        if(index>=bodies.size() || !bodies[index].alive ||
           !bodies[index].is_static || !ids ||
           count==0 || count!=bodies[index].triangle_count)
            throw std::invalid_argument("Invalid Jolt triangle material table");
        for(uint32_t n=0;n<count;++n)
            if(ids[n]>8)throw std::invalid_argument("Unknown physical material ID");
        bodies[index].triangle_materials.assign(ids,ids+count);
    }

    void add_mesh_materials(float const* vertices,uint32_t vertex_count,
                            uint32_t const* indices,uint32_t index_count,
                            uint32_t const* materials,uint32_t count) {
        if(!materials || !count || count!=index_count/3)
            throw std::invalid_argument("Invalid static mesh material count");
        for(uint32_t n=0;n<count;++n)
            if(materials[n]>8)throw std::invalid_argument("Unknown physical material ID");
        add_mesh(vertices,vertex_count,indices,index_count);
        static_mesh_materials.back().assign(materials,materials+count);
    }

    void set_object_type(uint32_t index,uint32_t type) {
        if(index>=bodies.size()||!bodies[index].alive||type>=16)
            throw std::invalid_argument("Invalid Jolt object collision type");
        bodies[index].object_type=type;
        auto& iface=physics.GetBodyInterface();
        auto layer=iface.GetObjectLayer(bodies[index].id);
        iface.SetObjectLayer(bodies[index].id,
            registry.with_type(layer,type));
    }
    uint32_t object_type(uint32_t index) const {
        return index<bodies.size() && bodies[index].alive?
            bodies[index].object_type:UINT32_MAX;
    }

    void configure_hulls(uint32_t index,
                         const NorthStarPhysicsCookedHull* hulls,uint32_t count) {
        if(index>=bodies.size()||!bodies[index].alive)
            throw std::invalid_argument("Invalid cooked collider owner");
        auto shape=northstarcpp::jolt_mesh::build_hulls(hulls,count);
        auto& body=bodies[index];
        auto& iface=physics.GetBodyInterface();
        bool const resident=body.resident;
        if(resident) {
            iface.RemoveBody(body.id);
            body.resident=false;
        }
        iface.SetShape(body.id,shape,true,j::EActivation::DontActivate);
        if(resident&&body.collision_enabled) {
            iface.AddBody(body.id,body.is_static?j::EActivation::DontActivate:
                                                  j::EActivation::Activate);
            body.resident=true;
        }
    }
    void configure_responses(uint32_t index,uint32_t type,
                             const std::uint8_t* responses,std::uint32_t count) {
        if(index>=bodies.size()||!bodies[index].alive||
           !responses||(count!=6&&count!=16))
            throw std::invalid_argument("Invalid Jolt response configuration");
        std::array<std::uint8_t,16> all{};
        all.fill(2);
        std::copy(responses,responses+count,all.begin());
        auto const layer=registry.assign(
            !bodies[index].is_static,type,all.data(),16);
        physics.GetBodyInterface().SetObjectLayer(bodies[index].id,layer);
        bodies[index].object_type=type;
    }
    void query_responses(uint32_t index,std::uint32_t* type,
                         std::uint8_t* responses,std::uint32_t count) const {
        if(index>=bodies.size()||!bodies[index].alive||
           !type||!responses||(count!=6&&count!=16))
            throw std::invalid_argument("Invalid Jolt response query");
        auto const layer=physics.GetBodyInterface().GetObjectLayer(bodies[index].id);
        auto const profile=registry.get(layer);
        *type=profile.type;
        std::copy_n(profile.responses.begin(),count,responses);
    }
    std::uint32_t drain_contacts(NorthStarPhysicsContactEvent* events,
                                 std::uint32_t capacity) {
        return contact_events.drain(events,capacity);
    }

    void set_static_resident(uint32_t index, bool resident)
    {
        if (index>=bodies.size() || !bodies[index].alive ||
            !bodies[index].is_static ||
            bodies[index].id==player)
            throw std::invalid_argument("Only scene static bodies may change Jolt residency");
        auto& body=bodies[index];
        if (!body.collision_enabled) return;
        if (body.resident==resident) return;
        auto& iface=physics.GetBodyInterface();
        if (resident) iface.AddBody(body.id,j::EActivation::DontActivate);
        else iface.RemoveBody(body.id);
        body.resident=resident;
    }

    int static_resident(uint32_t index) const
    {
        if (index>=bodies.size() || !bodies[index].alive ||
            !bodies[index].is_static ||
            bodies[index].id==player) return -1;
        return bodies[index].resident ? 1 : 0;
    }

    void add_mesh(float const* vertices,uint32_t vertex_count,
                  uint32_t const* indices,uint32_t index_count)
    {
        if(!vertices||!indices||vertex_count<3||vertex_count>1000000u||
           index_count<3||index_count>3000000u||index_count%3||
           static_meshes.size()>=256)
            throw std::invalid_argument("Invalid Jolt static mesh topology");
        for(uint32_t i=0;i<vertex_count*3u;++i)
            if(!std::isfinite(vertices[i]))
                throw std::invalid_argument("Jolt static triangle has non-finite point");
        j::TriangleList triangles;
        triangles.reserve(index_count/3);
        for(uint32_t i=0;i<index_count;i+=3) {
            j::Vec3 points[3];
            for(int k=0;k<3;++k) {
                uint32_t const index=indices[i+k];
                if(index>=vertex_count)
                    throw std::invalid_argument("Jolt triangle mesh index outside buffer");
                points[k]=j::Vec3(vertices[3*index],
                                  vertices[3*index+1],vertices[3*index+2]);
            }
            triangles.emplace_back(points[0],points[1],points[2],0,i/3);
        }
        j::MeshShapeSettings mesh(triangles);
        mesh.mPerTriangleUserData=true;
        auto created=mesh.Create();
        if(created.HasError())
            throw std::runtime_error("Jolt cannot construct triangle mesh shape");
        j::RefConst<j::Shape> shape=created.Get();
        j::BodyCreationSettings cfg(shape,j::RVec3::sZero(),
            j::Quat::sIdentity(),j::EMotionType::Static,layers::static_body);
        auto& iface=physics.GetBodyInterface();
        j::BodyID id=iface.CreateAndAddBody(cfg,j::EActivation::DontActivate);
        if(id.IsInvalid())
            throw std::runtime_error("Jolt cannot register triangle mesh body");
        static_meshes.push_back(id);
        static_mesh_materials.emplace_back();
        physics.OptimizeBroadPhase();
    }

    NorthStarPhysicsCastHit sweep(float sx,float sy,float sz,
        float ex,float ey,float ez,float radius,uint32_t ignored,
        NorthStarPhysicsSurfaceHit* details=nullptr,
        std::uint32_t query_channel=UINT32_MAX,
        std::uint32_t response_mask=4u) const
    {
        for(float v:{sx,sy,sz,ex,ey,ez,radius})
            if(!std::isfinite(v))
                throw std::invalid_argument("Non-finite Jolt sphere cast");
        if(radius<=0.f||radius>2.f||
           (ignored!=UINT32_MAX && ignored>=bodies.size()))
            throw std::invalid_argument("Invalid Jolt camera sweep radius/index");
        j::SphereShape shape(radius);
        j::RVec3 const start(sx,sy,sz);
        j::Vec3 const delta(ex-sx,ey-sy,ez-sz);
        j::RShapeCast cast(&shape,j::Vec3::sReplicate(1.f),
            j::RMat44::sTranslation(start),delta);
        j::ShapeCastSettings settings;
        settings.mBackFaceModeTriangles=j::EBackFaceMode::CollideWithBackFaces;
        settings.mBackFaceModeConvex=j::EBackFaceMode::CollideWithBackFaces;
        j::ClosestHitCollisionCollector<j::CastShapeCollector> collector;
        j::BodyID const excluded=ignored==UINT32_MAX
            ? j::BodyID() : bodies[ignored].id;
        j::IgnoreSingleBodyFilter filter(excluded);
        if(query_channel!=UINT32_MAX) {
            if(query_channel>=16||(response_mask&~6u)!=0||!(response_mask&6u))
                throw std::invalid_argument("Invalid sphere cast collision channel");
            northstarcpp::jolt_collision::QueryLayerFilter layer(
                registry,query_channel,response_mask);
            physics.GetNarrowPhaseQuery().CastShape(cast,settings,start,
                collector,{},layer,filter);
        } else
            physics.GetNarrowPhaseQuery().CastShape(cast,settings,start,
                collector,{}, {}, filter);
        NorthStarPhysicsCastHit out{};
        if(details) {
            *details={};
            details->triangle_index=UINT32_MAX;
        }
        if(collector.HadHit()) {
            auto const& hit=collector.mHit;
            out.hit=1;
            out.fraction=hit.mFraction;
            out.object_index=UINT32_MAX;
            for(uint32_t n=0;n<bodies.size();++n)
                if(bodies[n].id==hit.mBodyID2) {
                    out.object_index=n;
                    break;
                }
            // mPenetrationAxis points from the swept shape to the collider.
            j::Vec3 const normal=-hit.mPenetrationAxis.NormalizedOr(j::Vec3::sAxisY());
            out.normal_x=normal.GetX();
            out.normal_y=normal.GetY();
            out.normal_z=normal.GetZ();
            if(details) {
                auto shape_ref=physics.GetBodyInterface().GetShape(hit.mBodyID2);
                if(shape_ref && shape_ref->GetSubType()==j::EShapeSubType::Mesh) {
                    auto const* mesh=static_cast<j::MeshShape const*>(shape_ref.GetPtr());
                    auto const triangle=mesh->GetTriangleUserData(hit.mSubShapeID2);
                    details->triangle_index=triangle;
                    std::vector<uint32_t> const* table=nullptr;
                    if(out.object_index<bodies.size())
                        table=&bodies[out.object_index].triangle_materials;
                    else {
                        for(std::size_t n=0;n<static_meshes.size();++n)
                            if(static_meshes[n]==hit.mBodyID2) {
                                table=&static_mesh_materials[n];
                                break;
                            }
                    }
                    if(table && triangle<table->size())
                        details->material_id=(*table)[triangle];
                }
            }
        }
        if(details)details->cast=out;
        return out;
    }

    NorthStarPhysicsCastHit raycast_channel(
        float sx,float sy,float sz,float ex,float ey,float ez,
        std::uint32_t channel,std::uint32_t response_mask,
        std::uint32_t ignored) const {
        for(float v:{sx,sy,sz,ex,ey,ez})
            if(!std::isfinite(v))throw std::invalid_argument("Invalid ray coordinates");
        if(channel>=16||(response_mask&~6u)!=0||!(response_mask&6u)||
           (ignored!=UINT32_MAX&&ignored>=bodies.size()))
            throw std::invalid_argument("Invalid raycast collision channel");
        j::RRayCast ray(j::RVec3(sx,sy,sz),j::Vec3(ex-sx,ey-sy,ez-sz));
        j::RayCastResult hit;
        j::BodyID const excluded=ignored==UINT32_MAX?
            j::BodyID():bodies[ignored].id;
        j::IgnoreSingleBodyFilter body_filter(excluded);
        northstarcpp::jolt_collision::QueryLayerFilter layer(registry,
            channel,response_mask);
        NorthStarPhysicsCastHit result{};
        if(!physics.GetNarrowPhaseQuery().CastRay(ray,hit,{},layer,body_filter))
            return result;
        result.hit=1;
        result.fraction=hit.mFraction;
        result.object_index=UINT32_MAX;
        for(std::uint32_t i=0;i<bodies.size();++i)
            if(bodies[i].alive&&bodies[i].id==hit.mBodyID) {
                result.object_index=i;
                break;
            }
        j::BodyLockRead locked(physics.GetBodyLockInterface(),hit.mBodyID);
        if(locked.Succeeded()) {
            auto const normal=locked.GetBody().GetWorldSpaceSurfaceNormal(
                hit.mSubShapeID2,ray.GetPointOnRay(hit.mFraction));
            result.normal_x=normal.GetX();
            result.normal_y=normal.GetY();
            result.normal_z=normal.GetZ();
        }
        return result;
    }

    void read(NorthStarBodyPose* output, uint32_t count) const
    {
        if (count != bodies.size() || (count && !output))
            throw std::runtime_error("Invalid Jolt pose output buffer");
        auto const& iface = physics.GetBodyInterface();
        for (uint32_t n = 0; n < count; ++n) {
            auto const& body = bodies[n];
            if (!body.alive) {
                output[n] = {};
                output[n].rotation[0] = output[n].rotation[4] = output[n].rotation[8] = 1.f;
                continue;
            }
            j::RVec3 pos = iface.GetPosition(body.id);
            j::Quat rot = iface.GetRotation(body.id);
            j::Vec3 x = rot.RotateAxisX();
            j::Vec3 y = rot.RotateAxisY();
            j::Vec3 z = rot.RotateAxisZ();
            output[n] = {
                static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()) - body.half,
                static_cast<float>(pos.GetZ()),
                {x.GetX(), x.GetY(), x.GetZ(), y.GetX(), y.GetY(), y.GetZ(),
                 z.GetX(), z.GetY(), z.GetZ()}
            };
        }
    }

    #include "jolt_vehicle_methods.inc"

    Library& initialization;
    BroadPhaseLayers broad_phase;
    LayerVsBroadPhase layer_vs_broadphase;
    northstarcpp::jolt_collision::Registry registry;
    LayerPairs layer_pairs;
    northstarcpp::jolt_collision::Events contact_events;
    j::PhysicsSystem physics;
    j::JobSystemThreadPool jobs;
    // Preserve the fast 8 MiB arena, but fall back to malloc for contact-rich scenes.
    // A fixed TempAllocatorImpl aborts the process on pool exhaustion.
    j::TempAllocatorImplWithMallocFallback temporary_allocator;
    j::BodyID ground;
    j::BodyID player;
    std::uint32_t controlled_index=UINT32_MAX;
    std::unordered_map<std::uint32_t,NorthStarPhysicsCharacterMotor> motors;
    bool sprinting=false;
    std::vector<Body> bodies;
    uint32_t initial_body_count = 0;
    std::vector<j::BodyID> static_meshes;
    std::vector<std::vector<std::uint32_t>> static_mesh_materials;
    double elapsed = 0.0;
};

void* create(const NorthStarRigidBody* objects, uint32_t count)
{
    std::unique_ptr<World> w;
    if (!guarded([&] { w = std::make_unique<World>(objects, count); }))
        return nullptr;
    return w.release();
}
void destroy(void* world) { delete static_cast<World*>(world); }
int step(void* world, float seconds, float x, float z)
{
    return guarded([&] {
        if (!world) throw std::runtime_error("Null Jolt world");
        static_cast<World*>(world)->step(seconds, x, z);
    });
}
int read_poses(void* world, NorthStarBodyPose* poses, uint32_t count)
{
    return guarded([&] {
        if (!world) throw std::runtime_error("Null Jolt world");
        static_cast<World*>(world)->read(poses, count);
    });
}

int jump(void* world)
{
    return guarded([&] {
        if (!world) throw std::runtime_error("Null Jolt world");
        static_cast<World*>(world)->jump();
    });
}
int apply_impulse(void* world, uint32_t index, float x, float y, float z)
{
    return guarded([&] {
        if(!world) throw std::invalid_argument("Null Jolt impulse world");
        static_cast<World*>(world)->apply_impulse(index,x,y,z);
    });
}
int move_static(void* world, uint32_t index, float x, float y, float z)
{
    return guarded([&] {
        if (!world) throw std::runtime_error("Null Jolt world");
        static_cast<World*>(world)->move_static(index, x, y, z);
    });
}

int sweep_sphere(void* state,float sx,float sy,float sz,
    float ex,float ey,float ez,float radius,uint32_t ignored,NorthStarPhysicsCastHit* result)
{
    return guarded([&] {
        if(!state||!result) throw std::invalid_argument("Invalid Jolt sweep request");
        *result=static_cast<World const*>(state)->sweep(sx,sy,sz,ex,ey,ez,radius,ignored);
    });
}
int add_static_mesh(void* state,const float* vertices,uint32_t count,
    const uint32_t* indices,uint32_t index_count)
{
    return guarded([&] {
        if(!state) throw std::invalid_argument("Invalid Jolt static mesh world");
        static_cast<World*>(state)->add_mesh(vertices,count,indices,index_count);
    });
}
int set_static_resident(void* world,uint32_t index,uint32_t resident)
{
    if (resident>1u) return 0;
    return guarded([&] {
        if (!world) throw std::invalid_argument("Null Jolt collision-residency world");
        static_cast<World*>(world)->set_static_resident(index,resident!=0);
    });
}
int is_static_resident(void* world,uint32_t index)
{
    return world ? static_cast<World const*>(world)->static_resident(index) : -1;
}
const NorthStarPhysicsResidencyApi residency_api{
    NORTHSTARCPP_PHYSICS_RESIDENCY_ABI,set_static_resident,is_static_resident};
int add_runtime_body(void* world, NorthStarRigidBody const* body, uint32_t* index)
{
    return guarded([&] {
        if (!world || !body || !index)
            throw std::invalid_argument("Invalid Jolt runtime create arguments");
        *index = static_cast<World*>(world)->add_runtime_body(*body);
    });
}
int remove_runtime_body(void* world, uint32_t index)
{
    return guarded([&] {
        if (!world) throw std::invalid_argument("Missing Jolt runtime world");
        static_cast<World*>(world)->remove_runtime_body(index);
    });
}
int configure_collider(void* world,uint32_t index,
                       NorthStarPhysicsCollider const* spec)
{
    return guarded([&] {
        if(!world || !spec)throw std::invalid_argument("Null Jolt collider request");
        static_cast<World*>(world)->configure_collider(index,*spec);
    });
}
const NorthStarPhysicsColliderApi collider_api{
    NORTHSTARCPP_PHYSICS_COLLIDER_ABI,configure_collider};
int configure_mesh(void* world,uint32_t index,uint32_t mode,uint32_t splits,
                   float const* vertices,uint32_t vertex_count,
                   uint32_t const* indices,uint32_t index_count) {
    return guarded([&] {
        if(!world)throw std::invalid_argument("Null Jolt collision world");
        static_cast<World*>(world)->configure_mesh(
            index,mode,splits,vertices,vertex_count,indices,index_count);
    });
}
int set_object_type(void* world,uint32_t index,uint32_t type) {
    return guarded([&] {
        if(!world)throw std::invalid_argument("Null Jolt collision world");
        static_cast<World*>(world)->set_object_type(index,type);
    });
}
uint32_t object_type(void* world,uint32_t index) {
    return world?static_cast<World*>(world)->object_type(index):UINT32_MAX;
}
const NorthStarPhysicsMeshApi mesh_api{
    NORTHSTARCPP_PHYSICS_MESH_ABI,configure_mesh,set_object_type,object_type};
// Optional per-triangle material queries; legacy Physics ABI 4 is unchanged.
int set_triangle_materials(void* world,uint32_t index,
                           uint32_t const* ids,uint32_t count) {
    return guarded([&] {
        if(!world)throw std::invalid_argument("Null Jolt material world");
        static_cast<World*>(world)->set_triangle_materials(index,ids,count);
    });
}
int add_static_mesh_materials(void* world,float const* xyz,uint32_t verts,
                              uint32_t const* indices,uint32_t index_count,
                              uint32_t const* ids,uint32_t count) {
    return guarded([&] {
        if(!world)throw std::invalid_argument("Null Jolt surface mesh world");
        static_cast<World*>(world)->add_mesh_materials(
            xyz,verts,indices,index_count,ids,count);
    });
}
int sweep_sphere_surface(void* world,float sx,float sy,float sz,
                         float ex,float ey,float ez,float radius,
                         uint32_t ignored,NorthStarPhysicsSurfaceHit* out) {
    return guarded([&] {
        if(!world || !out)throw std::invalid_argument("Null Jolt surface sweep");
        static_cast<World const*>(world)->sweep(
            sx,sy,sz,ex,ey,ez,radius,ignored,out);
    });
}
const NorthStarPhysicsSurfaceApi surface_api{
    NORTHSTARCPP_PHYSICS_SURFACE_ABI,
    set_triangle_materials,add_static_mesh_materials,sweep_sphere_surface};

int configure_cooked_hulls(void* world,uint32_t index,
    const NorthStarPhysicsCookedHull* hulls,uint32_t count) {
    return guarded([&] {
        if(!world)throw std::invalid_argument("Null Jolt cooked collision world");
        static_cast<World*>(world)->configure_hulls(index,hulls,count);
    });
}
int configure_responses(void* world,uint32_t index,uint32_t type,
    const uint8_t responses[6]) {
    return guarded([&] {
        if(!world)throw std::invalid_argument("Null Jolt response world");
        static_cast<World*>(world)->configure_responses(index,type,responses,6);
    });
}
int query_responses(void* world,uint32_t index,uint32_t* type,
    uint8_t responses[6]) {
    return guarded([&] {
        if(!world)throw std::invalid_argument("Null Jolt response world");
        static_cast<World*>(world)->query_responses(index,type,responses,6);
    });
}
uint32_t drain_contacts(void* world,NorthStarPhysicsContactEvent* events,
                        uint32_t capacity) {
    if(!world)return 0;
    return static_cast<World*>(world)->drain_contacts(events,capacity);
}
int configure_channels(void* world,std::uint32_t index,std::uint32_t type,
    const std::uint8_t* responses,std::uint32_t count) {
    return guarded([&] {
        if(!world)throw std::invalid_argument("Null channels world");
        static_cast<World*>(world)->configure_responses(index,type,responses,count);
    });
}
int query_channels(void* world,std::uint32_t index,std::uint32_t* type,
    std::uint8_t* responses,std::uint32_t count) {
    return guarded([&] {
        if(!world)throw std::invalid_argument("Null channels query");
        static_cast<World const*>(world)->query_responses(index,type,responses,count);
    });
}
int sweep_sphere_channel(void* world,float sx,float sy,float sz,
    float ex,float ey,float ez,float radius,std::uint32_t channel,
    std::uint32_t mask,std::uint32_t ignored,NorthStarPhysicsCastHit* hit) {
    return guarded([&] {
        if(!world||!hit)throw std::invalid_argument("Null channel sphere cast");
        *hit=static_cast<World const*>(world)->sweep(
            sx,sy,sz,ex,ey,ez,radius,ignored,nullptr,channel,mask);
    });
}
int raycast_channel(void* world,float sx,float sy,float sz,
    float ex,float ey,float ez,std::uint32_t channel,
    std::uint32_t mask,std::uint32_t ignored,NorthStarPhysicsCastHit* hit) {
    return guarded([&] {
        if(!world||!hit)throw std::invalid_argument("Null channel raycast");
        *hit=static_cast<World const*>(world)->raycast_channel(
            sx,sy,sz,ex,ey,ez,channel,mask,ignored);
    });
}
const NorthStarPhysicsChannelsApi channels_api{
    NORTHSTARCPP_PHYSICS_CHANNELS_ABI,configure_channels,query_channels,
    sweep_sphere_channel,raycast_channel};
const NorthStarPhysicsCookedHullsApi cooked_hulls_api{
    NORTHSTARCPP_PHYSICS_COOKED_HULLS_ABI,configure_cooked_hulls};
const NorthStarPhysicsResponseApi response_api{
    NORTHSTARCPP_PHYSICS_RESPONSE_ABI,
    configure_responses,query_responses,drain_contacts};
const NorthStarPhysicsRuntimeApi runtime_api{
    NORTHSTARCPP_PHYSICS_RUNTIME_ABI, add_runtime_body, remove_runtime_body};

int create_vehicle(void* world,std::uint32_t index,
    NorthStarVehicleRig const* rig,float yaw) {
    return guarded([&]{
        if(!world||!rig)throw std::invalid_argument("Null vehicle setup");
        static_cast<World*>(world)->create_vehicle(index,*rig,yaw);
    });
}
int destroy_vehicle(void* world,std::uint32_t index) {
    return guarded([&]{
        if(!world)throw std::invalid_argument("Null vehicle world");
        static_cast<World*>(world)->destroy_vehicle(index);
    });
}
int set_vehicle_controls(void* world,std::uint32_t index,
    float throttle,float steer,float brake,float handbrake) {
    return guarded([&]{
        if(!world)throw std::invalid_argument("Null vehicle world");
        static_cast<World*>(world)->vehicle_controls(index,throttle,steer,brake,handbrake);
    });
}
int get_vehicle_pose(void* world,std::uint32_t index,NorthStarPhysicsVehiclePose* out) {
    return guarded([&]{
        if(!world||!out||out->size<sizeof(*out))
            throw std::invalid_argument("Missing vehicle pose buffer");
        static_cast<World const*>(world)->vehicle_pose(index,*out);
    });
}
int get_vehicle_wheel(void* world,std::uint32_t index,std::uint32_t ordinal,
    NorthStarVehicleWheelState* out) {
    return guarded([&]{
        if(!world||!out||out->size<sizeof(*out))
            throw std::invalid_argument("Missing vehicle wheel buffer");
        static_cast<World const*>(world)->vehicle_wheel(index,ordinal,*out);
    });
}
std::uint32_t get_vehicle_wheel_count(void* world,std::uint32_t index) {
    if(!world)return 0u;
    auto const& cars=static_cast<World const*>(world)->vehicles;
    auto at=cars.find(index);
    return at==cars.end()?0u:at->second.wheel_count;
}
NorthStarPhysicsVehicleApi const vehicle_api{
    NORTHSTARCPP_PHYSICS_VEHICLE_ABI,sizeof(NorthStarPhysicsVehicleApi),
    create_vehicle,destroy_vehicle,set_vehicle_controls,get_vehicle_pose,
    get_vehicle_wheel,get_vehicle_wheel_count
};
int possess_character_body(void* world,std::uint32_t index) {
    return guarded([&] {
        if(!world)throw std::invalid_argument("Null Jolt character world");
        static_cast<World*>(world)->possess_body(index);
    });
}
int configure_character_motor(void* world,std::uint32_t index,
    NorthStarPhysicsCharacterMotor const* settings) {
    return guarded([&] {
        if(!world || !settings)
            throw std::invalid_argument("Invalid Jolt character motor request");
        static_cast<World*>(world)->configure_motor(index,*settings);
    });
}
int set_character_sprint(void* world,std::uint32_t active) {
    return guarded([&] {
        if(!world || active>1u)
            throw std::invalid_argument("Invalid Jolt sprint state");
        static_cast<World*>(world)->set_sprinting(active!=0);
    });
}
const NorthStarPhysicsCharacterApi character_api{
    NORTHSTARCPP_PHYSICS_CHARACTER_ABI,possess_character_body,
    set_character_sprint,configure_character_motor};
const NorthStarPhysicsApi api{create, destroy, step, read_poses, jump, move_static,
    sweep_sphere, add_static_mesh, apply_impulse};
const NorthStarModule module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_PHYSICS, "Jolt", "5.6.0", NORTHSTARCPP_PHYSICS_ABI,
    sizeof(api), &api, last_error};
} // namespace

extern "C" const NorthStarModule* northstar_module_query(uint32_t host,
    char const* capability, uint32_t version)
{
    if(version==NORTHSTARCPP_PHYSICS_CHARACTER_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_PHYSICS,
            "Jolt","5.6.0",NORTHSTARCPP_PHYSICS_CHARACTER_MODULE_ABI,
            sizeof(character_api),&character_api,last_error};
        return northstarcpp::module_provider::match(
            extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_PHYSICS_VEHICLE_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_PHYSICS,
            "Jolt","5.6.0",NORTHSTARCPP_PHYSICS_VEHICLE_MODULE_ABI,
            sizeof(vehicle_api),&vehicle_api,last_error};
        return northstarcpp::module_provider::match(extension,host,capability,version);
    }
    if (version == NORTHSTARCPP_PHYSICS_SURFACE_MODULE_ABI) {
        static NorthStarModule const ext{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_PHYSICS,
            "Jolt","5.6.0",NORTHSTARCPP_PHYSICS_SURFACE_MODULE_ABI,
            sizeof(surface_api),&surface_api,last_error};
        return northstarcpp::module_provider::match(ext,host,capability,version);
    }
    if (version == NORTHSTARCPP_PHYSICS_CHANNELS_MODULE_ABI) {
        static NorthStarModule const ext{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_PHYSICS,
            "Jolt","5.6.0",NORTHSTARCPP_PHYSICS_CHANNELS_MODULE_ABI,
            sizeof(channels_api),&channels_api,last_error};
        return northstarcpp::module_provider::match(ext,host,capability,version);
    }
    if (version == NORTHSTARCPP_PHYSICS_COOKED_HULLS_MODULE_ABI) {
        static NorthStarModule const ext{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_PHYSICS,"Jolt","5.6.0",
            NORTHSTARCPP_PHYSICS_COOKED_HULLS_MODULE_ABI,
            sizeof(cooked_hulls_api),&cooked_hulls_api,last_error};
        return northstarcpp::module_provider::match(ext,host,capability,version);
    }
    if (version == NORTHSTARCPP_PHYSICS_RESPONSE_MODULE_ABI) {
        static NorthStarModule const ext{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_PHYSICS,"Jolt","5.6.0",
            NORTHSTARCPP_PHYSICS_RESPONSE_MODULE_ABI,
            sizeof(response_api),&response_api,last_error};
        return northstarcpp::module_provider::match(ext,host,capability,version);
    }
    if (version == NORTHSTARCPP_PHYSICS_MESH_MODULE_ABI) {
        static NorthStarModule const ext{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_PHYSICS,"Jolt","5.6.0",
            NORTHSTARCPP_PHYSICS_MESH_MODULE_ABI,sizeof(mesh_api),&mesh_api,last_error};
        return northstarcpp::module_provider::match(ext,host,capability,version);
    }
    if (version == NORTHSTARCPP_PHYSICS_COLLIDER_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_PHYSICS, "Jolt", "5.6.0",
            NORTHSTARCPP_PHYSICS_COLLIDER_MODULE_ABI, sizeof(collider_api),
            &collider_api, last_error};
        return northstarcpp::module_provider::match(extension, host, capability, version);
    }
    if (version == NORTHSTARCPP_PHYSICS_RUNTIME_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_PHYSICS, "Jolt", "5.6.0",
            NORTHSTARCPP_PHYSICS_RUNTIME_MODULE_ABI, sizeof(runtime_api), &runtime_api, last_error};
        return northstarcpp::module_provider::match(extension, host, capability, version);
    }
    if (version == NORTHSTARCPP_PHYSICS_RESIDENCY_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_PHYSICS, "Jolt", "5.6.0",
            NORTHSTARCPP_PHYSICS_RESIDENCY_MODULE_ABI, sizeof(residency_api), &residency_api, last_error};
        return northstarcpp::module_provider::match(extension, host, capability, version);
    }
    return northstarcpp::module_provider::match(module, host, capability, version);
}

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarPhysicsSurfaceApi const* northstar_physics_get_surface_api(
    uint32_t version)
{
    return version==NORTHSTARCPP_PHYSICS_SURFACE_ABI ? &surface_api : nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarPhysicsResidencyApi const* northstar_physics_get_residency_api(
    uint32_t version)
{
    return version==NORTHSTARCPP_PHYSICS_RESIDENCY_ABI ? &residency_api : nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarPhysicsColliderApi const* northstar_physics_get_collider_api(uint32_t version)
{
    return version==NORTHSTARCPP_PHYSICS_COLLIDER_ABI ? &collider_api : nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarPhysicsRuntimeApi const* northstar_physics_get_runtime_api(uint32_t version)
{
    return version==NORTHSTARCPP_PHYSICS_RUNTIME_ABI ? &runtime_api : nullptr;
}
