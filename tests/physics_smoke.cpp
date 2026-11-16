#include "qa_scene_fixture.hpp"
// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/physics.hpp>
#include <northstarcpp/collision_authoring.hpp>
#include <northstarcpp/collision_mesh.hpp>
#include <northstarcpp/collision_hulls.hpp>
#include <northstarcpp/collision_wireframe.hpp>
#include <northstarcpp/scene_world.hpp>

#include <cmath>
#include <cstdlib>
#include <string>
#include <iostream>
#include <limits>

namespace {

using namespace northstarcpp;

bool near(float actual, float expected, float tolerance = 0.035f)
{
    return std::abs(actual - expected) < tolerance;
}

void simulate(PhysicsScene& physics, int frames, float x = 0.f, float z = 0.f)
{
    for (int i = 0; i < frames; ++i)
        physics.step(1.f / 60.f, x, z);
}

int fail(char const* reason)
{
    std::cerr << reason << '\n';
    return 1;
}

} // namespace

int main()
{
    Scene const falling=qa::make({
        {"Floor block", {0.f, 0.f, 0.f}, 2.f, 0xffffff, false, 0.f},
        {"Stacked cube", {0.f, 5.f, 0.f}, 1.f, 0xffffff, false, 2.f},
        {"Ground cube", {5.f, 4.f, 0.f}, 1.f, 0xffffff, false, 3.f},
    });
    PhysicsScene gravity(falling);
    char const* requested_provider = std::getenv("NORTHSTARCPP_EXPECT_PHYSICS");
    std::string const provider = requested_provider ? requested_provider : "Jolt";
    std::string const version = provider == "Jolt" ? "5.6.0" : "3.25";
    if (gravity.provider_name() != provider || gravity.provider_version() != version)
        return fail("Unexpected physics provider or SDK version");
    simulate(gravity, 15);
    if (!(gravity.scene().objects[1].position.y < 4.8f))
        return fail("Gravity did not move the cube");
    simulate(gravity, 240);
    auto const& bodies = gravity.scene().objects;
    if (!near(bodies[1].position.y, 2.f) || !near(bodies[2].position.y, 0.f) ||
        bodies[0].position.y != 0.f || falling.objects[1].position.y != 5.f)
        return fail("Ground collision, stacking, or authored scene isolation failed");

    Scene const obstacle=qa::make({
        {"Player", {0.f, 0.f, 0.f}, 1.f, 0xffffff, true, 1.f},
        {"Wall", {2.f, 0.f, 0.f}, 1.f, 0xffffff, false, 0.f},
        {"Movable", {0.f, 0.f, 2.f}, 1.f, 0xffffff, false, 1.f},
    });
    PhysicsScene movement(obstacle);
    simulate(movement, 120, 1.f);
    if (!near(movement.scene().objects[0].position.x, 1.f) ||
        movement.scene().objects[1].position.x != 2.f)
        return fail("Player did not stop against a static body");
    PhysicsScene pushing(obstacle);
    simulate(pushing, 60, 0.f, 1.f);
    if (pushing.scene().objects[2].position.z < 2.4f)
        return fail("Player did not push the dynamic body");

    Scene const balanced=qa::make({
        {"Pedestal", {0.f, 0.f, 0.f}, 1.f, 0xffffff, false, 0.f},
        {"Unbalanced", {0.8f, 1.5f, 0.f}, 1.f, 0xffffff, false, 1.f},
    });
    PhysicsScene tipping(balanced);
    simulate(tipping, 22);
    auto const rotated_up = tipping.orientation(1).transform_direction({0.f, 1.f, 0.f});
    if (std::abs(rotated_up.x) < 0.05f || !near(rotated_up.magnitude(), 1.f))
        return fail("Collision did not produce a usable rigid body orientation");

    Scene const initial=qa::make({{"Player", {0.f, 0.f, 0.f}, 1.f, 0xffffff, true, 1.f}});
    PhysicsScene sixty(initial), one_twenty(initial);
    simulate(sixty, 60, 1.f, 1.f);
    for (int i = 0; i < 120; ++i)
        one_twenty.step(1.f / 120.f, 1.f, 1.f);
    auto const actual = sixty.scene().objects[0].position;
    auto const expected = one_twenty.scene().objects[0].position;
    if (!near(actual.x, std::sqrt(8.f)) || !near(actual.x, actual.z) ||
        !near(actual.x, expected.x) || !near(actual.y, expected.y))
        return fail("Fixed timestep or diagonal movement failed");

    sixty.step(std::numeric_limits<float>::quiet_NaN(), 1.f, 1.f);
    if (sixty.scene().objects[0].position.x != actual.x)
        return fail("Invalid elapsed time changed the scene");

    // The FPS jump is a real provider-side vertical velocity, not a camera-only effect.
    PhysicsScene jumper(initial);
    simulate(jumper, 12);
    jumper.jump();
    simulate(jumper, 12);
    if (jumper.scene().objects[0].position.y < 0.1f)
        return fail("Grounded player jump did not lift the physics body");
    simulate(jumper, 180);
    if (!near(jumper.scene().objects[0].position.y, 0.f))
        return fail("Jumping player did not land");

    // Animated target transforms must update the real static collision bodies.
    PhysicsScene animated(obstacle);
    animated.move_static(1, {4.f, 1.f, -3.f});
    simulate(animated, 4);
    if (!near(animated.scene().objects[1].position.x, 4.f) ||
        !near(animated.scene().objects[1].position.y, 1.f) ||
        !near(animated.scene().objects[1].position.z, -3.f))
        return fail("Static collision body relocation failed");
    try {
        animated.move_static(2, {2.f, 0.f, 2.f});
        return fail("Dynamic body was accepted as an animated static target");
    } catch (std::runtime_error const&) {}

    // Editor-authored collision shapes must reach the active Jolt physics provider.
    Scene collisionScene;
    collisionScene.version=3;
    SceneObject disabled;
    disabled.name="Collision disabled";
    disabled.node.id=701;
    disabled.node.collision=false;
    disabled.position={3.f,0.f,0.f};
    SceneObject sphere=disabled;
    sphere.name="Sphere collider";
    sphere.node.id=702;
    sphere.node.collision=true;
    sphere.position={7.f,0.f,0.f};
    collisionScene.objects={disabled,sphere};
    collision_authoring::Collider sphereShape;
    sphereShape.shape=collision_authoring::Shape::Sphere;
    sphereShape.dimensions={2.f,2.f,2.f};
    collision_authoring::assign(collisionScene,702,sphereShape);
    validate_scene(collisionScene);
    PhysicsScene authoredShapes(collisionScene);
    if(authoredShapes.sweep_sphere({3.f,1.8f,0.f},
                                  {3.f,.7f,0.f},.1f))
        return fail("Disabled actor still participates in physics collision");
    auto sphereHit=authoredShapes.sweep_sphere({7.f,2.8f,0.f},
                                              {7.f,1.5f,0.f},.1f);
    if(!sphereHit || sphereHit->object_index!=1)
        return fail("Authored sphere collider was not registered in physics");

    // Complex Mesh collision uses actual baked triangles, not a proxy box.
    Scene meshWorld;
    meshWorld.version=3;
    SceneObject complex;
    complex.name="Complex collision actor";
    complex.node.id=751;
    complex.node.asset="meshes/cube.obj";
    complex.node.collision=true;
    complex.mass=0.f;
    complex.size=1.f;
    complex.position={23.f,0.f,0.f};
    SceneObject convex=complex;
    convex.node.id=752;
    convex.name="Auto Convex actor";
    convex.mass=2.f;
    convex.position={28.f,3.f,0.f};
    meshWorld.objects={complex,convex};
    collision_mesh::Mesh cube;
    cube.source="meshes/cube.obj";
    cube.xyz={
        -.5f,0.f,-.5f, .5f,0.f,-.5f,
        -.5f,1.f,-.5f, .5f,1.f,-.5f,
        -.5f,0.f, .5f, .5f,0.f, .5f,
        -.5f,1.f, .5f, .5f,1.f, .5f};
    cube.indices={0,1,3,0,3,2,4,6,7,4,7,5,
                  0,4,5,0,5,1,2,3,7,2,7,6,
                  0,2,6,0,6,4,1,5,7,1,7,3};
    collision_authoring::Collider exact;
    exact.shape=collision_authoring::Shape::ComplexMesh;
    exact.object_type=collision_authoring::ObjectType::WorldStatic;
    collision_authoring::assign(meshWorld,751,exact);
    collision_authoring::Collider autoConvex;
    autoConvex.shape=collision_authoring::Shape::AutoConvex;
    autoConvex.splits_num=2;
    autoConvex.object_type=collision_authoring::ObjectType::PhysicsBody;
    collision_authoring::assign(meshWorld,752,autoConvex);
    collision_mesh::assign(meshWorld,751,cube);
    collision_mesh::assign(meshWorld,752,cube);
    collision_cooker::Hull cachedHull{cube.xyz,cube.indices};
    collision_hulls::assign(meshWorld,752,
        collision_hulls::Set{cube.source,2,{cachedHull}});
    auto const restored=parse_scene_v3(serialize_scene_v3(meshWorld));
    auto const restoredCollider=collision_authoring::lookup(restored,752);
    if(!restoredCollider ||
       restoredCollider->splits_num!=2 ||
       restoredCollider->object_type!=collision_authoring::ObjectType::PhysicsBody ||
       !collision_mesh::lookup(restored,751) ||
       !collision_hulls::lookup(restored,752))
        return fail("Complex collider or splitsNum lost after scene serialization");
    std::size_t wireSegments=0;
    collision_wireframe::draw(restored,restored.objects[0],
        [&](Vector3,Vector3){++wireSegments;},true);
    if(wireSegments!=cube.indices.size())
        return fail("Complex collision wireframe differs from baked triangles");
    PhysicsScene cooked(restored);
    auto complexHit=cooked.sweep_sphere(
        {23.f,2.f,0.f},{23.f,.55f,0.f},.1f);
    if(!complexHit || complexHit->object_index!=0)
        return fail("Complex mesh triangles do not produce collision");
    auto convexHit=cooked.sweep_sphere(
        {28.f,4.8f,0.f},{28.f,3.2f,0.f},.1f);
    if(!convexHit || convexHit->object_index!=1)
        return fail("Auto convex decomposition is not active in physics");

    // Collision response matrix must affect Jolt broadphase/contact sensor.
    auto response_scene=[](collision_authoring::Response static_to_dynamic) {
        Scene s;
        s.version=3;
        SceneObject wall;
        wall.name="Static response probe";
        wall.node.id=801;wall.node.collision=true;
        wall.position={41.f,0.f,0.f};wall.mass=0.f;
        wall.size=2.f;
        SceneObject movable=wall;
        movable.node.id=802;movable.name="Dynamic response probe";
        movable.position={41.f,.3f,0.f};movable.mass=1.f;
        s.objects={wall,movable};
        collision_authoring::Collider fixed;
        fixed.object_type=collision_authoring::ObjectType::WorldStatic;
        fixed.responses[1]=static_to_dynamic;
        collision_authoring::assign(s,801,fixed);
        collision_authoring::Collider moving;
        moving.object_type=collision_authoring::ObjectType::WorldDynamic;
        collision_authoring::assign(s,802,moving);
        return s;
    };
    for(auto response:{collision_authoring::Response::Ignore,
                       collision_authoring::Response::Overlap,
                       collision_authoring::Response::Block}) {
        PhysicsScene probe(response_scene(response));
        if(!probe.supports_collision_response_matrix())
            return fail("Jolt provider response matrix unavailable");
        simulate(probe,3);
        auto events=probe.poll_collision_events();
        bool observed=false;
        for(auto const& event:events) {
            bool pair=(event.first_index==0&&event.second_index==1)||
                      (event.first_index==1&&event.second_index==0);
            if(pair&&event.phase==0) {
                if(event.response!=static_cast<std::uint32_t>(response))
                    return fail("Collision event has wrong Block/Overlap type");
                observed=true;
            }
        }
        if(observed!=(response!=collision_authoring::Response::Ignore))
            return fail("Ignore/Overlap/Block broadphase or events incorrect");
        // Ignore dynamic object 1 so queries only inspect the static body
        // and test the authored response to WorldDynamic (channel 1).
        auto ray=probe.raycast_channel(
            {41.f,4.f,0.f},{41.f,.5f,0.f},1u,false,1u);
        auto cast=probe.sweep_sphere_channel(
            {41.f,4.f,0.f},{41.f,.5f,0.f},.1f,1u,false,1u);
        if(bool(ray)!=(response==collision_authoring::Response::Block) ||
           bool(cast)!=(response==collision_authoring::Response::Block))
            return fail("Channel-aware ray/sweep Block filtering failed");
        auto overlapRay=probe.raycast_channel(
            {41.f,4.f,0.f},{41.f,.5f,0.f},1u,true,1u);
        if(bool(overlapRay)!=(response!=collision_authoring::Response::Ignore))
            return fail("Channel-aware overlap query filtering failed");
        if(response==collision_authoring::Response::Overlap) {
            probe.move_static(0,{70.f,0.f,0.f});
            simulate(probe,3);
            auto removed=probe.poll_collision_events();
            bool ended=false;
            for(auto const& event:removed)
                if(event.phase==1 && event.response==1 &&
                   ((event.first_index==0&&event.second_index==1)||
                    (event.first_index==1&&event.second_index==0)))
                    ended=true;
            if(!ended)return fail("Overlap End event not delivered");
        }
    }

    // Custom project channel 7 is genuinely enforced by Jolt query filtering.
    auto customScene=response_scene(collision_authoring::Response::Block);
    auto custom=collision_authoring::for_object(customScene,customScene.objects[0]);
    custom.object_type=static_cast<collision_authoring::ObjectType>(7);
    custom.responses[7]=collision_authoring::Response::Ignore;
    custom.preset="Interactable";
    collision_authoring::assign(customScene,customScene.objects[0].node.id,custom);
    auto restoredCustom=parse_scene_v3(serialize_scene_v3(customScene));
    auto migrated=collision_authoring::lookup(restoredCustom,801);
    if(!migrated||migrated->responses[7]!=collision_authoring::Response::Ignore ||
       migrated->preset!="Interactable" ||
       std::uint32_t(migrated->object_type)!=7)
        return fail("Project custom collision channel lost after scene save/load");
    PhysicsScene customWorld(restoredCustom);
    if(customWorld.raycast_channel({41.f,4.f,0.f},{41.f,.5f,0.f},7,false,1))
        return fail("Custom channel Ignore raycast accepted");
    custom.responses[7]=collision_authoring::Response::Block;
    collision_authoring::assign(restoredCustom,801,custom);
    PhysicsScene blockingCustom(restoredCustom);
    auto customHit=blockingCustom.raycast_channel(
        {41.f,4.f,0.f},{41.f,.5f,0.f},7,false,1);
    if(!customHit||customHit->object_index!=0 || customHit->response!=2)
        return fail("Custom channel Block raycast was not registered");

    // Full host -> SceneWorld -> optional provider-residency path. Both
    // The Jolt provider must drop far static collision and restore it on
    // fast travel while the player's/dynamic body's ID stays valid.
    Scene authored;
    authored.version=3;
    authored.name="Streaming collision regression";
    authored.streaming={25.f,40.f,0.f};
    authored.cells={
        {"Near",{-10.f,-4.f,-10.f},{20.f,8.f,10.f},false},
        {"Far",{90.f,-4.f,-10.f},{110.f,8.f,10.f},false}
    };
    SceneObject resident_player;
    resident_player.name="Driver"; resident_player.node.id=1;
    resident_player.node.cell="Near"; // Pawn is a scene component rather than a flag.
    resident_player.mass=1.f; resident_player.position={10.f,0.f,0.f};
    SceneObject static_near;
    static_near.name="Roadside"; static_near.node.id=2;
    static_near.node.cell="Near"; static_near.mass=0.f;
    static_near.position={0.f,1.f,0.f}; static_near.size=2.f;
    SceneObject static_far=static_near;
    static_far.name="Distant wall"; static_far.node.id=3;
    static_far.node.cell="Far"; static_far.position={100.f,1.f,0.f};
    authored.objects={resident_player,static_near,static_far};
    scene_pawn::migrate_imported_local(authored,1);
    validate_scene(authored);
    PhysicsScene simulated(authored);
    SceneWorld streaming(authored);
    if (!simulated.supports_scene_residency())
        return fail("Provider lacks optional static collision-residency API");
    auto cast_far=[&]() {
        return simulated.sweep_sphere({100.f,5.f,0.f},
                                      {100.f,2.f,0.f},0.2f);
    };
    auto collision=cast_far();
    if (!collision || collision->object_index!=2)
        return fail("Authored remote body missing before streaming");
    streaming.update({10.f,0.f,0.f});
    if (simulated.sync_scene_residency(streaming,{10.f,0.f,0.f})!=1)
        return fail("Streaming failed to detach far static collision");
    if (cast_far())
        return fail("Far static body remains collidable after unload");
    if (simulated.sync_scene_residency(streaming,{10.f,0.f,0.f})!=0)
        return fail("Unchanged streaming unnecessarily modifies bodies");
    streaming.update({100.f,0.f,0.f});
    if (simulated.sync_scene_residency(streaming,{100.f,0.f,0.f})<1)
        return fail("Fast travel failed to restore remote collision");
    collision=cast_far();
    if (!collision || collision->object_index!=2 ||
        simulated.scene().objects[2].node.id!=3)
        return fail("Fast-travel collision restoration changed stable body IDs");
    // Runtime entities must join the SAME provider world without resetting
    // authored bodies, player movement, or World Partition source indices.
    PhysicsScene runtime(authored);
    if (!runtime.supports_runtime_entities())
        return fail("Provider lacks incremental runtime entity ABI");
    SceneObject spawned;
    spawned.name="Script crate";
    spawned.node.id=9001;
    spawned.position={30.f,1.f,0.f};
    spawned.size=2.f;
    spawned.mass=0.f;
    auto const index=runtime.spawn_runtime(spawned);
    if (index!=authored.objects.size() || !runtime.runtime_alive(index) ||
        runtime.authored_object_count()!=authored.objects.size() ||
        runtime.authored_scene().objects.size()!=authored.objects.size())
        return fail("Runtime spawn changed authored scene topology");
    auto cast_spawned=[&]() {
        return runtime.sweep_sphere({30.f,5.f,0.f},{30.f,2.f,0.f},0.2f);
    };
    auto spawned_hit=cast_spawned();
    if (!spawned_hit || spawned_hit->object_index!=index)
        return fail("Runtime-spawned static entity has no real collision");
    SceneWorld runtime_streaming(authored);
    runtime_streaming.update({10.f,0.f,0.f}, {}, {}, &runtime.authored_scene());
    (void)runtime.sync_scene_residency(runtime_streaming,{10.f,0.f,0.f});
    if (!cast_spawned())
        return fail("WorldPartition accidentally evicted persistent runtime body");
    runtime.move_static(index,{32.f,1.f,0.f});
    if (cast_spawned() ||
        !runtime.sweep_sphere({32.f,5.f,0.f},{32.f,2.f,0.f},0.2f))
        return fail("Runtime transform did not update collision");
    runtime.destroy_runtime(index);
    if (runtime.runtime_alive(index) ||
        runtime.sweep_sphere({32.f,5.f,0.f},{32.f,2.f,0.f},0.2f) ||
        runtime.scene().objects[index].node.visible)
        return fail("Runtime destroy left a collidable or visible body");
    try {
        runtime.destroy_runtime(index);
        return fail("Stale runtime entity handle was accepted");
    } catch (std::out_of_range const&) {}
    spawned.node.id=9002;
    spawned.name="Falling script crate";
    spawned.position={32.f,5.f,5.f};
    spawned.mass=2.f;
    auto const dynamic_index=runtime.spawn_runtime(spawned);
    if (dynamic_index<=index || !runtime.runtime_alive(dynamic_index))
        return fail("Runtime body ID was reused");
    simulate(runtime,30);
    if (!(runtime.scene().objects[dynamic_index].position.y<4.5f))
        return fail("Runtime dynamic physics body did not fall");
    runtime.destroy_runtime(dynamic_index);
    simulate(runtime,2);
    if (runtime.scene().objects.size()!=dynamic_index+1 ||
        runtime.authored_scene().objects.size()!=authored.objects.size())
        return fail("Runtime deletion invalidated scene indices");
    // Stress the provider-side append/remove path without world rebuilds.
    // Entities are placed far apart, so the test measures lifecycle overhead
    // without accidentally creating an O(n^2) contact-pair workload.
    std::vector<std::size_t> bulk_indices;
    bulk_indices.reserve(1000);
    for (std::size_t n=0;n<1000;++n) {
        SceneObject prop;
        prop.name="Runtime prop";
        prop.node.id=10000+n;
        prop.position={200.f+static_cast<float>(n)*4.f,1.f,800.f};
        prop.size=1.f;
        auto const slot=runtime.spawn_runtime(prop);
        if (slot!=dynamic_index+1+n)
            return fail("Runtime bulk spawn reused an entity index");
        bulk_indices.push_back(slot);
    }
    if (runtime.scene().objects.size()!=authored.objects.size()+1002u)
        return fail("Runtime bulk spawn count mismatch");
    simulate(runtime,2);
    for (auto const slot:bulk_indices)
        runtime.destroy_runtime(slot);
    simulate(runtime,2);
    for (auto const slot:bulk_indices)
        if (runtime.runtime_alive(slot))
            return fail("Runtime bulk deletion leaked an active physics slot");
    if (runtime.authored_scene().objects.size()!=authored.objects.size())
        return fail("Mass runtime deletion changed authored scene");
    return 0;
}
