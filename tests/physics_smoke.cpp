// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/physics.hpp>
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
    Scene const falling{{
        {"Floor block", {0.f, 0.f, 0.f}, 2.f, 0xffffff, false, 0.f},
        {"Stacked cube", {0.f, 5.f, 0.f}, 1.f, 0xffffff, false, 2.f},
        {"Ground cube", {5.f, 4.f, 0.f}, 1.f, 0xffffff, false, 3.f},
    }};
    PhysicsScene gravity(falling);
    char const* requested_provider = std::getenv("NORTHSTARCPP_EXPECT_PHYSICS");
    std::string const provider = requested_provider ? requested_provider : "Bullet";
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

    Scene const obstacle{{
        {"Player", {0.f, 0.f, 0.f}, 1.f, 0xffffff, true, 1.f},
        {"Wall", {2.f, 0.f, 0.f}, 1.f, 0xffffff, false, 0.f},
        {"Movable", {0.f, 0.f, 2.f}, 1.f, 0xffffff, false, 1.f},
    }};
    PhysicsScene movement(obstacle);
    simulate(movement, 120, 1.f);
    if (!near(movement.scene().objects[0].position.x, 1.f) ||
        movement.scene().objects[1].position.x != 2.f)
        return fail("Player did not stop against a static body");
    PhysicsScene pushing(obstacle);
    simulate(pushing, 60, 0.f, 1.f);
    if (pushing.scene().objects[2].position.z < 2.4f)
        return fail("Player did not push the dynamic body");

    Scene const balanced{{
        {"Pedestal", {0.f, 0.f, 0.f}, 1.f, 0xffffff, false, 0.f},
        {"Unbalanced", {0.8f, 1.5f, 0.f}, 1.f, 0xffffff, false, 1.f},
    }};
    PhysicsScene tipping(balanced);
    simulate(tipping, 22);
    auto const rotated_up = tipping.orientation(1).transform_direction({0.f, 1.f, 0.f});
    if (std::abs(rotated_up.x) < 0.05f || !near(rotated_up.magnitude(), 1.f))
        return fail("Collision did not produce a usable rigid body orientation");

    Scene const initial{{{"Player", {0.f, 0.f, 0.f}, 1.f, 0xffffff, true, 1.f}}};
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

    // Full host -> SceneWorld -> optional provider-residency path. Both
    // Bullet and Jolt must drop far static collision and restore it on
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
    resident_player.node.cell="Near"; resident_player.player=true;
    resident_player.mass=1.f; resident_player.position={10.f,0.f,0.f};
    SceneObject static_near;
    static_near.name="Roadside"; static_near.node.id=2;
    static_near.node.cell="Near"; static_near.mass=0.f;
    static_near.position={0.f,1.f,0.f}; static_near.size=2.f;
    SceneObject static_far=static_near;
    static_far.name="Distant wall"; static_far.node.id=3;
    static_far.node.cell="Far"; static_far.position={100.f,1.f,0.f};
    authored.objects={resident_player,static_near,static_far};
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
    return 0;
}
