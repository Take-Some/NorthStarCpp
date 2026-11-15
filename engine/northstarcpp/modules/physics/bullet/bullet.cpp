// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/physics_plugin.h>
#include "module_provider.hpp"

#include <btBulletDynamicsCommon.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using namespace northstarcpp::module_provider;

constexpr float player_speed = 4.f;
constexpr double fixed_step = 1.0 / 120.0;
constexpr double max_frame_time = 0.1;

struct Body {
    std::unique_ptr<btCollisionShape> shape;
    std::unique_ptr<btRigidBody> rigid;
    float half = 0.f;
    bool resident = true;
    bool alive = true;
};

Body make_body(std::unique_ptr<btCollisionShape> shape, float mass, btVector3 const& center)
{
    btVector3 inertia(0, 0, 0);
    if (mass > 0.f)
        shape->calculateLocalInertia(mass, inertia);

    btRigidBody::btRigidBodyConstructionInfo setup(mass, nullptr, shape.get(), inertia);
    setup.m_startWorldTransform.setIdentity();
    setup.m_startWorldTransform.setOrigin(center);
    setup.m_friction = 0.65f;
    return {std::move(shape), std::make_unique<btRigidBody>(setup)};
}

float axis_input(float value)
{
    return std::isfinite(value) ? std::clamp(value, -1.f, 1.f) : 0.f;
}

struct StaticMesh {
    // BVH shape borrows the triangle storage: keep the mesh alive until after
    // the rigid body/shape has been destroyed.
    std::unique_ptr<btTriangleMesh> triangles;
    std::unique_ptr<btBvhTriangleMeshShape> shape;
    std::unique_ptr<btRigidBody> rigid;
};

struct World {
    World()
    {
        world.setGravity(btVector3(0, -9.81f, 0));
    }

    ~World()
    {
        for (auto const& mesh : static_meshes)
            world.removeRigidBody(mesh.rigid.get());
        for (auto const& body : bodies)
            if (body.alive && body.resident)
                world.removeRigidBody(body.rigid.get());
        if (ground.rigid)
            world.removeRigidBody(ground.rigid.get());
    }

    void initialize(NorthStarRigidBody const* objects, uint32_t count)
    {
        initial_body_count = count;
        if (count && !objects)
            throw std::runtime_error("Missing rigid bodies");
        bodies.reserve(count);
        ground = make_body(std::make_unique<btStaticPlaneShape>(btVector3(0, 1, 0), 0.f),
            0.f, btVector3(0, 0, 0));
        world.addRigidBody(ground.rigid.get());

        for (uint32_t i = 0; i < count; ++i) {
            auto const& object = objects[i];
            float const half = object.size * 0.5f;
            auto shape = std::make_unique<btBoxShape>(btVector3(half, half, half));
            bodies.push_back(make_body(std::move(shape), object.mass,
                btVector3(object.x, object.y + half, object.z)));
            bodies.back().half = half;
            auto* rigid = bodies.back().rigid.get();
            if (object.mass > 0.f) {
                rigid->setCcdMotionThreshold(half);
                rigid->setCcdSweptSphereRadius(half * 0.8f);
            }
            if (object.player) {
                player = rigid;
                rigid->setAngularFactor(btVector3(0, 0, 0));
                rigid->setFriction(0.f);
            }
            rigid->setUserIndex(static_cast<int>(i));
            world.addRigidBody(rigid);
        }
    }

    void step(float seconds, float input_x, float input_z)
    {
        if (!std::isfinite(seconds) || seconds <= 0.f)
            return;

        float const x = axis_input(input_x);
        float const z = axis_input(input_z);
        float const speed = player_speed / std::max(1.f, std::hypot(x, z));
        elapsed += std::min(static_cast<double>(seconds), max_frame_time);
        while (elapsed >= fixed_step) {
            if (player) {
                if (x != 0.f || z != 0.f)
                    player->activate();
                if (player->isActive()) {
                    auto const vertical = player->getLinearVelocity().y();
                    player->setLinearVelocity(btVector3(x * speed, vertical, z * speed));
                }
            }
            world.stepSimulation(static_cast<btScalar>(fixed_step), 0);
            elapsed -= fixed_step;
        }
    }

    bool grounded() const
    {
        if (!player || std::abs(player->getLinearVelocity().y()) > 0.65f)
            return false;
        auto const p = player->getWorldTransform().getOrigin();
        float const half = static_cast<btBoxShape const*>(player->getCollisionShape())->getHalfExtentsWithoutMargin().y()
            + player->getCollisionShape()->getMargin();
        float const bottom = p.y() - half;
        if (std::abs(bottom) < 0.13f) return true; // implicit y=0 floor
        for (auto const& body : bodies) {
            if (!body.resident || body.rigid.get() == player ||
                !body.rigid->isStaticObject()) continue;
            auto const o = body.rigid->getWorldTransform().getOrigin();
            if (std::abs(bottom - (o.y() + body.half)) <= 0.13f &&
                std::abs(p.x() - o.x()) < half + body.half - 0.05f &&
                std::abs(p.z() - o.z()) < half + body.half - 0.05f)
                return true;
        }
        return false;
    }

    void jump()
    {
        if (!player || !grounded()) return;
        auto v = player->getLinearVelocity();
        v.setY(5.35f);
        player->setLinearVelocity(v);
        player->activate(true);
    }

    void apply_impulse(uint32_t index, float x, float y, float z)
    {
        if(index>=bodies.size() || !bodies[index].alive ||
            bodies[index].rigid.get()==player ||
           bodies[index].rigid->isStaticOrKinematicObject() ||
           !std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z) ||
           std::hypot(x,y,z)>100.f)
            throw std::invalid_argument("Invalid Bullet dynamic-body impulse");
        auto& body=*bodies[index].rigid;
        body.activate(true);
        body.applyCentralImpulse(btVector3(x,y,z));
    }

    void move_static(uint32_t index, float x, float y, float z)
    {
        if (index >= bodies.size() || !bodies[index].alive ||
            !std::isfinite(x) || !std::isfinite(y) ||
            !std::isfinite(z) || !bodies[index].rigid->isStaticObject())
            throw std::runtime_error("Invalid Bullet static body relocation");
        auto& body = *bodies[index].rigid;
        auto transform = body.getWorldTransform();
        transform.setOrigin(btVector3(x, y + bodies[index].half, z));
        body.setWorldTransform(transform);
        body.setInterpolationWorldTransform(transform);
        if (bodies[index].resident)
            world.updateSingleAabb(&body);
    }

    uint32_t add_runtime_body(NorthStarRigidBody const& object)
    {
        if (bodies.size() >= 16384u || object.player ||
            !std::isfinite(object.x) || !std::isfinite(object.y) ||
            !std::isfinite(object.z) || !std::isfinite(object.size) ||
            !std::isfinite(object.mass) || object.size <= 0.f ||
            object.size > 1000.f || object.mass < 0.f || object.mass > 1000000.f)
            throw std::invalid_argument("Invalid Bullet runtime body");
        auto const half = object.size * 0.5f;
        auto next = make_body(std::make_unique<btBoxShape>(btVector3(half, half, half)),
            object.mass, btVector3(object.x, object.y + half, object.z));
        next.half = half;
        auto const index = static_cast<uint32_t>(bodies.size());
        next.rigid->setUserIndex(static_cast<int>(index));
        if (object.mass > 0.f) {
            next.rigid->setCcdMotionThreshold(half);
            next.rigid->setCcdSweptSphereRadius(half * 0.8f);
        }
        bodies.push_back(std::move(next));
        world.addRigidBody(bodies.back().rigid.get());
        return index;
    }

    void remove_runtime_body(uint32_t index)
    {
        if (index < initial_body_count || index >= bodies.size() ||
            !bodies[index].alive)
            throw std::invalid_argument("Stale or authored Bullet body index");
        auto& entry = bodies[index];
        if (entry.resident) world.removeRigidBody(entry.rigid.get());
        entry.alive = false;
        entry.resident = false;
        entry.rigid.reset();
        entry.shape.reset();
    }

    void set_static_resident(uint32_t index, bool resident)
    {
        if (index>=bodies.size() || !bodies[index].alive ||
            bodies[index].rigid.get()==player ||
            !bodies[index].rigid->isStaticObject())
            throw std::invalid_argument("Only scene static bodies may change Bullet residency");
        auto& body=bodies[index];
        if (body.resident==resident) return;
        if (resident) world.addRigidBody(body.rigid.get());
        else world.removeRigidBody(body.rigid.get());
        body.resident=resident;
    }

    int static_resident(uint32_t index) const
    {
        if (index>=bodies.size() || !bodies[index].alive ||
            bodies[index].rigid.get()==player ||
            !bodies[index].rigid->isStaticObject()) return -1;
        return bodies[index].resident ? 1 : 0;
    }

    void add_mesh(const float* vertices, uint32_t vertex_count,
                  const uint32_t* indices, uint32_t index_count)
    {
        if (!vertices || !indices || vertex_count<3 || vertex_count>1000000u ||
            index_count<3 || index_count>3000000u || index_count%3 ||
            static_meshes.size() >= 256)
            throw std::invalid_argument("Invalid Bullet triangle mesh");
        for (uint32_t i=0;i<vertex_count*3u;++i)
            if (!std::isfinite(vertices[i]))
                throw std::invalid_argument("Non-finite Bullet triangle vertex");
        StaticMesh result;
        result.triangles=std::make_unique<btTriangleMesh>(true,false);
        for (uint32_t i=0;i<index_count;i+=3) {
            btVector3 points[3];
            for (int j=0;j<3;++j) {
                uint32_t const index=indices[i+j];
                if (index>=vertex_count)
                    throw std::invalid_argument("Triangle mesh vertex index exceeds array");
                points[j]={vertices[3*index],vertices[3*index+1],vertices[3*index+2]};
            }
            // Physical geometry stays in world coordinates, with identity transform.
            result.triangles->addTriangle(points[0],points[1],points[2],true);
        }
        result.shape=std::make_unique<btBvhTriangleMeshShape>(result.triangles.get(),true);
        btVector3 inertia(0,0,0);
        btRigidBody::btRigidBodyConstructionInfo cfg(0.f,nullptr,result.shape.get(),inertia);
        result.rigid=std::make_unique<btRigidBody>(cfg);
        result.rigid->setUserIndex(-1);
        world.addRigidBody(result.rigid.get());
        static_meshes.push_back(std::move(result));
    }

    NorthStarPhysicsCastHit sweep(float sx,float sy,float sz,
        float ex,float ey,float ez,float radius,uint32_t ignored) const
    {
        for (float v:{sx,sy,sz,ex,ey,ez,radius})
            if (!std::isfinite(v))
                throw std::invalid_argument("Invalid Bullet sphere query");
        if(radius<=0.f||radius>2.f||
            (ignored!=UINT32_MAX && (ignored>=bodies.size() || !bodies[ignored].alive)))
            throw std::invalid_argument("Invalid Bullet camera sweep radius/index");
        auto const* ignored_body = ignored==UINT32_MAX
            ? nullptr : bodies[ignored].rigid.get();
        struct Filter final : btCollisionWorld::ClosestConvexResultCallback {
            explicit Filter(btVector3 const& from,btVector3 const& to,
                btCollisionObject const* excluded)
                : btCollisionWorld::ClosestConvexResultCallback(from,to), excluded_(excluded){}
            bool needsCollision(btBroadphaseProxy* proxy) const override {
                if (proxy->m_clientObject==excluded_) return false;
                return btCollisionWorld::ClosestConvexResultCallback::needsCollision(proxy);
            }
            btCollisionObject const* excluded_ = nullptr;
        };
        btVector3 const from(sx,sy,sz),to(ex,ey,ez);
        btSphereShape sphere(radius);
        btTransform start,end;
        start.setIdentity();end.setIdentity();
        start.setOrigin(from);end.setOrigin(to);
        Filter callback(from,to,ignored_body);
        world.convexSweepTest(&sphere,start,end,callback);
        NorthStarPhysicsCastHit hit{};
        if(callback.hasHit()) {
            hit.hit=1;
            int const idx=callback.m_hitCollisionObject->getUserIndex();
            hit.object_index=idx>=0 ? static_cast<uint32_t>(idx):UINT32_MAX;
            hit.fraction=callback.m_closestHitFraction;
            auto const normal=callback.m_hitNormalWorld;
            hit.normal_x=normal.x();hit.normal_y=normal.y();hit.normal_z=normal.z();
        }
        return hit;
    }

    void read(NorthStarBodyPose* output, uint32_t count) const
    {
        if (count != bodies.size() || (count && !output))
            throw std::runtime_error("Invalid pose buffer");
        for (uint32_t i = 0; i < count; ++i) {
            auto const& body = bodies[i];
            if (!body.alive) {
                output[i] = {};
                output[i].rotation[0] = output[i].rotation[4] = output[i].rotation[8] = 1.f;
                continue;
            }
            auto const& transform = body.rigid->getWorldTransform();
            auto const& center = transform.getOrigin();
            auto const& basis = transform.getBasis();
            auto const x = basis.getColumn(0);
            auto const y = basis.getColumn(1);
            auto const z = basis.getColumn(2);
            output[i] = {center.x(), center.y() - body.half, center.z(),
                {x.x(), x.y(), x.z(), y.x(), y.y(), y.z(), z.x(), z.y(), z.z()}};
        }
    }

    btDefaultCollisionConfiguration configuration;
    btCollisionDispatcher dispatcher{&configuration};
    btDbvtBroadphase broadphase;
    btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world{&dispatcher, &broadphase, &solver, &configuration};
    Body ground;
    std::vector<Body> bodies;
    uint32_t initial_body_count = 0;
    std::vector<StaticMesh> static_meshes;
    btRigidBody* player = nullptr;
    double elapsed = 0.0;
};

void* create(NorthStarRigidBody const* objects, uint32_t count)
{
    std::unique_ptr<World> result;
    if (!guarded([&] {
        result = std::make_unique<World>();
        result->initialize(objects, count);
    }))
        return nullptr;
    return result.release();
}

void destroy(void* world) { delete static_cast<World*>(world); }

int step(void* world, float seconds, float x, float z)
{
    return guarded([&] { static_cast<World*>(world)->step(seconds, x, z); });
}

int read_poses(void* world, NorthStarBodyPose* poses, uint32_t count)
{
    return guarded([&] { static_cast<World*>(world)->read(poses, count); });
}

int jump(void* world)
{
    return guarded([&] {
        if (!world) throw std::runtime_error("Missing Bullet world");
        static_cast<World*>(world)->jump();
    });
}

int apply_impulse(void* world, uint32_t index, float x, float y, float z)
{
    return guarded([&] {
        if (!world) throw std::invalid_argument("Null Bullet impulse world");
        static_cast<World*>(world)->apply_impulse(index, x, y, z);
    });
}

int move_static(void* world, uint32_t index, float x, float y, float z)
{
    return guarded([&] {
        if (!world) throw std::runtime_error("Missing Bullet world");
        static_cast<World*>(world)->move_static(index, x, y, z);
    });
}

int sweep_sphere(void* state,float sx,float sy,float sz,
    float ex,float ey,float ez,float radius,uint32_t ignored,NorthStarPhysicsCastHit* result)
{
    return guarded([&] {
        if(!state||!result)throw std::invalid_argument("Null Bullet sweep query");
        *result=static_cast<World const*>(state)->sweep(sx,sy,sz,ex,ey,ez,radius,ignored);
    });
}
int add_static_mesh(void* state,const float* vertices,uint32_t vertex_count,
    const uint32_t* indices,uint32_t index_count)
{
    return guarded([&] {
        if(!state)throw std::invalid_argument("Null Bullet world for triangle geometry");
        static_cast<World*>(state)->add_mesh(vertices,vertex_count,indices,index_count);
    });
}
int set_static_resident(void* world,uint32_t index,uint32_t resident)
{
    if (resident>1u) return 0;
    return guarded([&] {
        if (!world) throw std::invalid_argument("Null Bullet collision-residency world");
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
            throw std::invalid_argument("Invalid runtime body creation arguments");
        *index = static_cast<World*>(world)->add_runtime_body(*body);
    });
}
int remove_runtime_body(void* world, uint32_t index)
{
    return guarded([&] {
        if (!world) throw std::invalid_argument("Missing runtime physics world");
        static_cast<World*>(world)->remove_runtime_body(index);
    });
}
const NorthStarPhysicsRuntimeApi runtime_api{
    NORTHSTARCPP_PHYSICS_RUNTIME_ABI, add_runtime_body, remove_runtime_body};

static_assert(BT_BULLET_VERSION == 325, "Update Bullet provider metadata for a new SDK");
const NorthStarPhysicsApi api{create, destroy, step, read_poses, jump, move_static,
    sweep_sphere,add_static_mesh,apply_impulse};
const NorthStarModule module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_PHYSICS, "Bullet", "3.25", NORTHSTARCPP_PHYSICS_ABI,
    sizeof(api), &api, last_error};

} // namespace

extern "C" const NorthStarModule* northstar_module_query(uint32_t host,
    char const* capability, uint32_t version)
{
    if (version == NORTHSTARCPP_PHYSICS_RUNTIME_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_PHYSICS, "Bullet", "3.25",
            NORTHSTARCPP_PHYSICS_RUNTIME_MODULE_ABI, sizeof(runtime_api), &runtime_api, last_error};
        return match(extension, host, capability, version);
    }
    if (version == NORTHSTARCPP_PHYSICS_RESIDENCY_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_PHYSICS, "Bullet", "3.25",
            NORTHSTARCPP_PHYSICS_RESIDENCY_MODULE_ABI, sizeof(residency_api), &residency_api, last_error};
        return match(extension, host, capability, version);
    }
    return match(module, host, capability, version);
}

// Opt-in extension does not change the existing physics provider ABI.
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
