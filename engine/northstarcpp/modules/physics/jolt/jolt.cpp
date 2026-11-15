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
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Body/BodyFilter.h>

#include <northstarcpp/physics_plugin.h>
#include "module_provider.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
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
constexpr j::ObjectLayer static_body = 0;
constexpr j::ObjectLayer dynamic_body = 1;
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
        return layer == layers::static_body ? broadphase::static_body : broadphase::dynamic_body;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(j::BroadPhaseLayer layer) const override
    {
        return layer == broadphase::static_body ? "static" : "dynamic";
    }
#endif
};

class LayerPairs final : public j::ObjectLayerPairFilter {
public:
    bool ShouldCollide(j::ObjectLayer a, j::ObjectLayer b) const override
    {
        return a == layers::dynamic_body || b == layers::dynamic_body;
    }
};

class LayerVsBroadPhase final : public j::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(j::ObjectLayer a, j::BroadPhaseLayer b) const override
    {
        return a == layers::dynamic_body || b == broadphase::dynamic_body;
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
};

struct World {
    explicit World(NorthStarRigidBody const* objects, uint32_t count)
        : initialization(library()),
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
            setup.mFriction = input.player ? 0.f : 0.65f;
            if (input.mass > 0.f) {
                setup.mMotionQuality = j::EMotionQuality::LinearCast;
                setup.mOverrideMassProperties = j::EOverrideMassProperties::CalculateInertia;
                setup.mMassPropertiesOverride.mMass = input.mass;
            }
            if (input.player) {
                setup.mAllowedDOFs = j::EAllowedDOFs::TranslationX |
                    j::EAllowedDOFs::TranslationY | j::EAllowedDOFs::TranslationZ;
            }
            j::BodyID id = iface.CreateAndAddBody(setup, input.mass > 0.f ?
                j::EActivation::Activate : j::EActivation::DontActivate);
            if (id.IsInvalid())
                throw std::runtime_error("Jolt rigid body creation failed");
            bodies.push_back({id, half, input.mass == 0.f});
            if (input.player)
                player = id;
        }
        physics.OptimizeBroadPhase();
    }

    ~World()
    {
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

    void step(float delta, float input_x, float input_z)
    {
        if (!std::isfinite(delta) || delta <= 0.f)
            return;

        float x = sanitized_input(input_x);
        float z = sanitized_input(input_z);
        float speed = player_speed / std::max(1.f, std::hypot(x, z));
        elapsed += std::min(static_cast<double>(delta), max_frame_time);
        auto& iface = physics.GetBodyInterface();
        while (elapsed + 1.0e-9 >= fixed_step) {
            if (!player.IsInvalid()) {
                if (x != 0.f || z != 0.f)
                    iface.ActivateBody(player);
                if (physics.GetBodyInterface().IsActive(player)) {
                    float vertical = iface.GetLinearVelocity(player).GetY();
                    iface.SetLinearVelocity(player, j::Vec3(x * speed, vertical, z * speed));
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
        iface.SetLinearVelocity(player, j::Vec3(v.GetX(), 5.35f, v.GetZ()));
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
        if (bodies.size() >= 16384u || input.player ||
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

    void set_static_resident(uint32_t index, bool resident)
    {
        if (index>=bodies.size() || !bodies[index].alive ||
            !bodies[index].is_static ||
            bodies[index].id==player)
            throw std::invalid_argument("Only scene static bodies may change Jolt residency");
        auto& body=bodies[index];
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
            triangles.emplace_back(points[0],points[1],points[2]);
        }
        j::MeshShapeSettings mesh(triangles);
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
        physics.OptimizeBroadPhase();
    }

    NorthStarPhysicsCastHit sweep(float sx,float sy,float sz,
        float ex,float ey,float ez,float radius,uint32_t ignored) const
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
        physics.GetNarrowPhaseQuery().CastShape(cast,settings,start,
            collector,{}, {}, filter);
        NorthStarPhysicsCastHit out{};
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
        }
        return out;
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

    Library& initialization;
    BroadPhaseLayers broad_phase;
    LayerVsBroadPhase layer_vs_broadphase;
    LayerPairs layer_pairs;
    j::PhysicsSystem physics;
    j::JobSystemThreadPool jobs;
    j::TempAllocatorImpl temporary_allocator;
    j::BodyID ground;
    j::BodyID player;
    std::vector<Body> bodies;
    uint32_t initial_body_count = 0;
    std::vector<j::BodyID> static_meshes;
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
const NorthStarPhysicsRuntimeApi runtime_api{
    NORTHSTARCPP_PHYSICS_RUNTIME_ABI, add_runtime_body, remove_runtime_body};

const NorthStarPhysicsApi api{create, destroy, step, read_poses, jump, move_static,
    sweep_sphere, add_static_mesh, apply_impulse};
const NorthStarModule module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_PHYSICS, "Jolt", "5.6.0", NORTHSTARCPP_PHYSICS_ABI,
    sizeof(api), &api, last_error};
} // namespace

extern "C" const NorthStarModule* northstar_module_query(uint32_t host,
    char const* capability, uint32_t version)
{
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
NorthStarPhysicsResidencyApi const* northstar_physics_get_residency_api(
    uint32_t version)
{
    return version==NORTHSTARCPP_PHYSICS_RESIDENCY_ABI ? &residency_api : nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarPhysicsRuntimeApi const* northstar_physics_get_runtime_api(uint32_t version)
{
    return version==NORTHSTARCPP_PHYSICS_RUNTIME_ABI ? &runtime_api : nullptr;
}
