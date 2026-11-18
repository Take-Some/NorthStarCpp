#include "../engine/northstarcpp/modules/host/terrain_physics_bridge.hpp"
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

int main(int argc,char** argv)
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

    // Continuously translating CityRoot decks carry a physically possessed
    // pawn in world coordinates; static terrain and teleports do not.
    if(provider=="Jolt") {
        Scene carrier_scene=qa::make({
            {"Moving Bastion deck",{0.f,0.f,0.f},6.f,0x556677,false,0.f},
            {"Possessed passenger",{0.f,6.f,0.f},1.f,0xffffff,true,2.f},
        });
        PhysicsScene carrier(carrier_scene);
        simulate(carrier,12); // settle first, then begin drive
        for(int frame=0;frame<60;++frame) {
            carrier.move_static(0,{.10f*float(frame+1),0.f,0.f});
            carrier.step(1.f/60.f,0.f,0.f);
        }
        auto const deck=carrier.scene().objects[0].position;
        auto const passenger=carrier.scene().objects[1].position;
        if(!near(deck.x,6.f,.07f) ||
           std::abs(passenger.x-deck.x)>.45f ||
           std::abs(passenger.y-6.f)>.25f)
            return fail("Moving city support did not carry grounded Pawn");
        // Teleporting a support is not physical driving and must not throw
        // the player many metres along the world.
        float const passenger_before=passenger.x;
        carrier.move_static(0,{70.f,0.f,0.f});
        carrier.step(1.f/60.f,0.f,0.f);
        if(std::abs(carrier.scene().objects[1].position.x-passenger_before)>.9f)
            return fail("Support teleport incorrectly imparted city velocity");
    }

    if(argc==2 && (std::string(argv[1])=="--city-native-drive" ||
                   std::string(argv[1])=="--city-native-terrain" ||
                   std::string(argv[1])=="--city-native-longdrive")) {
        bool const longDrive=std::string(argv[1])=="--city-native-longdrive";
        // Real Jolt 6-DOF chassis with 25 million kg inertia (not a moving
        // kinematic box or a ghost engine.vehicle integration).
        Scene const scene=qa::make({
            {"Bastion physical chassis",{0.f,0.f,0.f},.25f,0x667788,false,0.f},
            {"Passenger",{0.f,6.85f,0.f},1.f,0xffffff,true,2.f},
            {"Cargo A",{-2.f,6.85f,-2.f},1.f,0x776655,false,3.f},
            {"Cargo B",{2.f,6.85f,3.f},1.f,0x335588,false,12.f}
        });
        PhysicsScene world(scene);
        std::unique_ptr<TerrainPhysicsBridge> terrain;
        if(std::string(argv[1])=="--city-native-terrain" || longDrive ||
           std::getenv("NORTHSTARCPP_RUSTSEA_TERRAIN")){
            world::TerrainConfig cfg;
            cfg.seed=20261117u;cfg.width_m=64.;cfg.radius=4;
            cfg.quads_per_axis=32;cfg.collision_quads=16;
            cfg.max_lod=3;cfg.base_height_m=0.;cfg.vertical_scale_m=5.;
            terrain=std::make_unique<TerrainPhysicsBridge>(cfg);
            terrain->update(world,{0,0,0});
            world::TerrainProvider field(cfg);
            for(float x:{-23.f,23.f}){
                std::cout<<"Terrain x="<<x<<" heights=";
                for(float z:{-21.f,-14.f,-7.f,0.f,7.f,14.f,21.f})
                    std::cout<<field.height(x,z)<<",";
                std::cout<<'\n';
            }
        }
        if(!world.supports_native_vehicle())
            return fail("Jolt physical vehicle extension missing");
        std::vector<NorthStarVehicleWheelConfig> wheels(14);
        for(std::size_t n=0;n<wheels.size();++n){
            auto& w=wheels[n];
            w.size=sizeof(w);
            w.mount[0]=n<7?-23.f:23.f;
            w.mount[1]=2.85f;
            w.mount[2]=-21.f+7.f*float(n%7);
            w.radius=1.88f;
            w.rest_length=std::getenv("NORTHSTARCPP_RUSTSEA_SKIP_WHEEL")?.03f:(std::getenv("NORTHSTARCPP_RUSTSEA_REST")?std::atof(std::getenv("NORTHSTARCPP_RUSTSEA_REST")):.85f);
            w.travel=std::getenv("NORTHSTARCPP_RUSTSEA_SKIP_WHEEL")?.03f:(std::getenv("NORTHSTARCPP_RUSTSEA_TRAVEL")?std::atof(std::getenv("NORTHSTARCPP_RUSTSEA_TRAVEL")):1.30f);
            w.spring_rate=std::getenv("NORTHSTARCPP_RUSTSEA_SPRING")?
                std::atof(std::getenv("NORTHSTARCPP_RUSTSEA_SPRING")):50000000.f;
            w.damper_rate=std::getenv("NORTHSTARCPP_RUSTSEA_DAMPER")?
                std::atof(std::getenv("NORTHSTARCPP_RUSTSEA_DAMPER")):12500000.f;
            w.grip_long=1.9f;w.grip_lat=2.3f;
            w.brake_factor=1.f;w.drive_factor=1.f;
            w.rotational_inertia=500000.f;
        }
        NorthStarVehicleRig rig{};
        rig.size=sizeof(rig);
        rig.mass=25000000.f;
        rig.yaw_inertia=12000000000.f;
        rig.center_of_mass_y=1.2f;
        rig.engine_force=std::getenv("NORTHSTARCPP_RUSTSEA_FORCE")?
            std::atof(std::getenv("NORTHSTARCPP_RUSTSEA_FORCE")):45000000.f;
        rig.brake_force=70000000.f;
        rig.aerodynamic_drag=10.f;
        rig.rolling_resistance=.012f;
        rig.abs_enabled=1;
        rig.traction_control_enabled=1;
        rig.wheel_count=14;
        rig.wheels=wheels.data();
        world.create_native_vehicle(0,rig,0.f);
        world.native_vehicle_controls(0,1.f,0.f,0.f);
        auto initial=world.native_vehicle_pose(0);
        float maxLocalDrift=0.f;
        const int frames=longDrive?7200:480;
        for(int i=0;i<frames;++i) {
            if(longDrive && terrain && i%15==0){
                auto const current=world.native_vehicle_pose(0);
                auto const chunk=terrain->update(world,
                    {current.x,current.y,current.z},
                    {current.linear_velocity[0],0.,current.linear_velocity[2]});
                if(chunk.resident_count!=81 ||
                   world.terrain_resident_count()!=81)
                    return fail("City streamed collider ring lost active chunks");
            }
            world.step(1.f/60.f,0.f,0.f);
            if(longDrive && i%300==299){
                auto const current=world.native_vehicle_pose(0);
                auto const& pawn=world.scene().objects[1].position;
                maxLocalDrift=std::max(maxLocalDrift,
                    std::abs((pawn.z-current.z)-0.f));
                std::cout<<"LONGDRIVE second="<<(i+1)/60
                         <<" city_z="<<current.z
                         <<" local_pawn_z="<<pawn.z-current.z
                         <<" max_drift="<<maxLocalDrift<<'\n';
            }
            if(terrain && (i==0||i==1||i==2||i==10||i==60||i==120)){
                auto p=world.native_vehicle_pose(0);
                std::cout<<"TRACE frame="<<i<<" y="<<p.y
                         <<" vy="<<p.linear_velocity[1]<<'\n';
            }
        }
        auto result=world.native_vehicle_pose(0);
        std::cout<<"Bastion Jolt physical drive start z="<<initial.z
                 <<" end z="<<result.z
                 <<" height="<<result.y
                 <<" wheelCount="<<world.native_vehicle_wheel_count(0)<<'\n';
        auto const& passenger=world.scene().objects[1].position;
        std::cout<<"Passenger z="<<passenger.z<<" y="<<passenger.y<<'\n';
        if(longDrive && (result.z-initial.z<600.f ||
           maxLocalDrift>.1f))
            return fail("Long-drive player/carrier drift or city stopped");
        if(!std::isfinite(result.z)||!std::isfinite(result.y)||
           result.z-initial.z<2.f)
            return fail("25,000 tonne dynamic Jolt city did not move");
        if(std::abs(passenger.z-result.z)>2.f ||
           std::abs(passenger.y-(result.y+6.85f))>1.f)
            return fail("Dynamic city deck did not carry player through contact");
        for(std::size_t n:{2u,3u}) {
            auto const& cargo=world.scene().objects[n].position;
            std::cout<<"City dynamic cargo "<<n<<" x="<<cargo.x
                     <<" y="<<cargo.y<<" z="<<cargo.z<<'\n';
            if(std::abs(cargo.z-result.z)>6.f ||
               std::abs(cargo.y-(result.y+6.85f))>1.f)
                return fail("Physical city failed to carry loose Jolt cargo");
        }
        if(!longDrive) {
            // A dynamic city is still valid ground for a jumping Pawn.
            float const before_jump=world.scene().objects[1].position.y;
            auto const before_vehicle=world.native_vehicle_pose(0);
            world.jump();
            for(int i=0;i<14;++i)world.step(1.f/60.f,0.f,0.f);
            auto const airborne=world.scene().objects[1].position;
            auto const vehicle=world.native_vehicle_pose(0);
            if(airborne.y<before_jump+.4f ||
               std::abs(airborne.z-vehicle.z)>1.5f)
                return fail("Dynamic deck jump lost airborne carrier inertia");
            std::cout<<"JUMP from dynamic city PASS: rise="
                     <<airborne.y-before_jump
                     <<" relative_z="<<airborne.z-vehicle.z<<'\n';
        }
        world.native_vehicle_controls(0,0.f,0.f,1.f);
        for(int i=0;i<240;++i)world.step(1.f/60.f,0.f,0.f);
        return 0;
    }
    // Dedicated CityRoot integration acceptance, independent of unrelated
    // legacy combat / wall-collision regression scenarios.
    if(argc==2 && std::string(argv[1])=="--city-carrier") {
        Scene const city=qa::make({
            {"Kinematic deck", {0.f,0.f,0.f},6.f,0x555555,false,0.f},
            {"Possessed passenger", {0.f,6.f,0.f},1.f,0xffffff,true,2.f},
            {"Loose cargo A", {1.4f,6.f,0.f},1.f,0x008855,false,3.f},
            {"Loose cargo B", {-1.4f,6.f,1.f},1.f,0xaa5500,false,7.f},
            {"Fixed world cargo", {22.f,0.f,0.f},1.f,0x6699ee,false,6.f}
        });
        PhysicsScene moving(city);
        simulate(moving,40);
        auto const static_start=moving.scene().objects[4].position.x;
        for(int frame=0;frame<180;++frame){
            moving.move_static(0,{(frame+1)*.05f,0.f,0.f}); // 3 m/s
            moving.step(1.f/60.f,0.f,0.f);
        }
        auto const& first=moving.scene().objects;
        std::cerr<<"carrier_steady deck="<<first[0].position.x
            <<" passenger="<<first[1].position.x
            <<" crateA="<<first[2].position.x
            <<" crateB="<<first[3].position.x<<" world="<<first[4].position.x<<'\n';
        if(std::abs(first[1].position.x-first[0].position.x)>.75f ||
           std::abs((first[2].position.x-first[0].position.x)-1.4f)>1.7f ||
           std::abs((first[3].position.x-first[0].position.x)+1.4f)>1.7f ||
           std::abs(first[4].position.x-static_start)>.08f ||
           std::abs(first[2].position.y-6.f)>.4f ||
           std::abs(first[3].position.y-6.f)>.4f)
            return fail("Kinematic deck failed to carry all grounded dynamic bodies");
        auto const player_before=moving.scene().objects[1].position.x;
        moving.jump();
        for(int frame=0;frame<18;++frame){
            moving.move_static(0,{9.f+(frame+1)*.05f,0.f,0.f});
            moving.step(1.f/60.f,0.f,0.f);
        }
        auto const airborne=moving.scene().objects[1].position;
        if(airborne.y<6.20f || airborne.x<player_before+.2f)
            return fail("Jump did not preserve onboard player's world-space inertia");
        // Drive stops: friction decelerates loose cargo, without having to
        // move each prop individually through scripting or a parent transform.
        simulate(moving,100);
        auto const& stopped=moving.scene().objects;
        std::cerr<<"carrier_stopped deck="<<stopped[0].position.x
            <<" passenger="<<stopped[1].position.x
            <<" crateA="<<stopped[2].position.x
            <<" crateB="<<stopped[3].position.x<<'\n';
        if(std::abs(stopped[2].position.x-stopped[0].position.x)>3.f ||
           std::abs(stopped[3].position.x-stopped[0].position.x)>3.f)
            return fail("Free cargo failed to brake on moving city");
        float const old_player=stopped[1].position.x;
        float const old_cargo=stopped[2].position.x;
        moving.move_static(0,{200.f,0.f,0.f}); // teleport, not a drive step
        moving.step(1.f/60.f,0.f,0.f);
        if(std::abs(moving.scene().objects[1].position.x-old_player)>1.f ||
           std::abs(moving.scene().objects[2].position.x-old_cargo)>1.f)
            return fail("Teleport erroneously carried player/cargo");
        std::cout<<"Jolt moving CityRoot: Pawn, two dynamic props, fixed world, "
                 <<"inertia, brakes, and teleport PASS\n";
        return 0;
    }

    if(argc==2 && std::string(argv[1])=="--city-drive-stress") {
        Scene const city=qa::make({
            {"Continuous CityRoot deck", {0.f,0.f,0.f},14.f,0x555555,false,0.f},
            {"Pawn on deck", {0.f,14.f,0.f},1.f,0xffffff,true,2.f},
            {"Unattached dynamic crate", {2.f,14.f,1.f},1.f,0x005500,false,4.f},
            {"World-anchored rock", {50.f,0.f,0.f},2.f,0x777777,false,8.f}
        });
        PhysicsScene moving(city);
        simulate(moving,35);
        float track=0.f, worst_pawn=0.f, worst_crate=0.f;
        for(int frame=0;frame<1500;++frame) {
            float const t=frame/60.f;
            float const speed=t<8.f ? 3.2f*t : t<19.f ? 25.6f :
                std::max(0.f,25.6f-(t-19.f)*5.f);
            track+=speed/60.f;
            moving.move_static(0,{0.f,0.f,track});
            moving.step(1.f/60.f,0.f,0.f);
            auto const& objs=moving.scene().objects;
            worst_pawn=std::max(worst_pawn,std::abs(objs[1].position.z-track));
            worst_crate=std::max(worst_crate,
                std::abs(objs[2].position.z-track-1.f));
            if(!std::isfinite(objs[1].position.y)||!std::isfinite(objs[2].position.y) ||
               std::abs(objs[3].position.x-50.f)>.02f ||
               std::abs(objs[1].position.y-14.f)>1.f ||
               std::abs(objs[2].position.y-14.f)>1.f) {
                std::cerr<<"city_stress FAILED frame="<<frame
                    <<" deck="<<track<<" pawn="<<objs[1].position.z
                    <<" crate="<<objs[2].position.z
                    <<" y="<<objs[1].position.y<<"/"<<objs[2].position.y<<'\n';
                return fail("City motion instability lost player or prop");
            }
        }
        std::cout<<"city_drive_stress 25s "<<"distance="<<track
                 <<" worst_pawn_offset="<<worst_pawn
                 <<" worst_crate_offset="<<worst_crate<<'\n';
        if(worst_pawn>2.f || worst_crate>4.f)
            return fail("High-speed Bastion loses player or cargo");
        return 0;
    }

    if(argc==2 && std::string(argv[1])=="--streamed-terrain") {
        northstarcpp::world::TerrainConfig profile;
        profile.seed=20261117u;
        profile.width_m=64.0;
        profile.radius=2;
        profile.base_height_m=-1.3;
        profile.vertical_scale_m=5.;
        profile.quads_per_axis=32;
        profile.collision_quads=16;
        profile.max_lod=3;
        northstarcpp::world::TerrainProvider field(profile);
        Scene scene=qa::make({
            {"Pawn", {0.f,6.f,0.f},1.f,0x4499bb,true,2.f}
        });
        PhysicsScene physics(scene);
        if(!physics.supports_streamed_terrain())return fail(
              "Loaded physics provider does not implement streamed terrain ABI");
        northstarcpp::TerrainPhysicsBridge bridge(profile);
        std::uint64_t upserts=0,evictions=0;
        for(int tick=0;tick<=24;++tick) {
            const double z=64.*tick;
            auto delta=bridge.update(physics,{0,0,z},{0,0,24});
            upserts+=delta.uploaded.size();
            evictions+=delta.evicted.size();
            if(delta.resident_count!=25 ||
               physics.terrain_resident_count()!=25)
                return fail("Terrain streaming residency must remain 5x5");
            auto const x=16.f;
            float const sample=float(z+19.);
            auto hit=physics.sweep_sphere({x,8.f,sample},
                {x,-8.f,sample},.2f,0);
            if(!hit)
                return fail("New Jolt heightfield chunk lacks terrain contact");
            float const hit_y=8.f-16.f*hit->fraction-.2f;
            float const analytic=float(field.height(x,sample));
            if(std::abs(hit_y-analytic)>.45f) {
                std::cerr<<"terrain hit error at z="<<z
                    <<" collision="<<hit_y<<" noise="<<analytic<<'\n';
                return fail("Terrain renderer and collision height disagree");
            }
        }
        bridge.unload(physics);
        if(physics.terrain_resident_count()!=0)return fail(
              "Terrain collision chunks leaked after unload");
        std::cout<<"Jolt streamed terrain: PASS 25 resident chunks, 1.5km travel, "
                 <<upserts<<" upserts, "<<evictions<<" evictions, "
                 <<"height matching world TerrainProvider"<<'\n';
        return 0;
    }

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
