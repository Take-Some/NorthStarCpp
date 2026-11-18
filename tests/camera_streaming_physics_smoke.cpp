#include "qa_scene_fixture.hpp"
// Bullet / Jolt integration: vehicle camera under streaming, teleport and fast travel.
#include <northstarcpp/camera.hpp>
#include <northstarcpp/physics.hpp>
#include <northstarcpp/scene_world.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using namespace northstarcpp;
namespace {
void require(bool ok,char const* reason) {
    if(!ok)throw std::runtime_error(reason);
}
void add_wall(PhysicsScene& physics,float x) {
    physics.add_static_mesh({
        {x-8.f,0.f,-3.f},{x+8.f,0.f,-3.f},
        {x-8.f,12.f,-3.f},{x+8.f,12.f,-3.f}},
        {0,1,2,1,3,2});
}
}
int main() {
    try {
        Scene authored;
        authored.version=3;
        authored.name="Highway Two-Sector Camera QA";
        authored.layers={{"Default",true}};
        authored.streaming={125.f,190.f,.9f};
        authored.cells={
            {"Highway West",{-70.f,-15.f,-90.f},{70.f,30.f,90.f},false},
            {"Highway East",{730.f,-15.f,-90.f},{870.f,30.f,90.f},false}
        };
        SceneObject bus;
        bus.name="Bus";
        // Vehicle is dynamic and has a nonzero mass when authored as Pawn.
        bus.mass=1200.f;
        bus.node.id=101;bus.node.cell="Highway West";
        authored.objects.push_back(bus);
    scene_pawn::migrate_imported_local(authored,101);
        SceneObject distant;
        distant.name="Streaming marker";
        distant.position={800.f,0.f,4.f};
        distant.node.id=102;distant.node.cell="Highway East";
        distant.node.collision=false;
        authored.objects.push_back(distant);
        SceneObject traffic;
        traffic.name="Physics traffic";
        traffic.position={3.f,0.f,40.f};
        traffic.mass=250.f;
        traffic.node.id=103;
        traffic.node.cell="Highway West";
        authored.objects.push_back(traffic);
        validate_scene(authored);
        auto const serial=serialize_scene_v3(authored);
        SceneWorld world(authored);
        PhysicsScene physics(authored);
        add_wall(physics,0.f);
        add_wall(physics,800.f);
        CameraSettings settings;
        settings.follow_spring_frequency_hz=2.6f;
        settings.follow_smoothing=.14f;
        settings.vehicle_lookahead_seconds=.18f;
        settings.lookahead_damping_seconds=.12f;
        settings.teleport_cut_distance=25.f;
        settings.collision_restore_seconds=.1f;
        settings.collision_padding=.08f;
        settings.vehicle_distance=7.5f;
        settings.vehicle_height=2.6f;
        CameraDirector camera(settings);
        camera.set_mode(CameraMode::vehicle_chase,0.f);

        CameraTarget target;
        target.eye={0.f,1.6f,0.f};
        target.heading=0.f;
        target.velocity={95.f,0.f,0.f}; // motorway speed; drive predicts new cell
        target.steering=.35f;
        target.ignore_object=0;
        world.update(target.eye,target.velocity);
        require(world.cell_loaded("Highway West"),
                "Current highway cell did not become resident");
        require(!world.cell_loaded("Highway East"),
                "Faraway cell was incorrectly prefetched");
        CameraPose pose{};
        double greatest_step=0;
        Scene live=authored;
        for(int frame=0;frame<720;++frame) {
            float const dt=1.f/120.f;
            target.eye.x+=target.velocity.x*dt;
            live.objects[0].position.x=target.eye.x;
            live.objects[2].position.x=target.eye.x+3.f;
            pose=camera.update(dt,target,&live,&physics);
            world.update(target.eye,target.velocity,{},&live);
            require(std::isfinite(pose.position.x) &&
                    std::isfinite(pose.position.y) &&
                    std::isfinite(pose.position.z) &&
                    pose.fov_y_degrees>=settings.default_fov_y,
                    "Invalid camera pose in 120 FPS high-speed chase");
            auto const collision=physics.sweep_sphere(
                target.eye,pose.position,settings.collision_radius,
                target.ignore_object);
            require(!collision || collision->fraction>=.99f,
                    "High-speed camera moved behind a wall");
        }

        require(world.cell_loaded("Highway East"),
                "Velocity lookahead did not prefetch approaching sector");
        live.objects[0].position={800.f,0.f,0.f};
        live.objects[2].position={803.f,0.f,40.f};
        target.eye={800.f,1.6f,0.f};
        target.velocity={0.f,0.f,0.f};
        world.update(target.eye,{}, {}, &live);
        // Player is deliberately pinned and retains authored ownership;
        // ordinary dynamic traffic is rehomed without losing its stable ID.
        require(world.cell_loaded("Highway East") &&
                world.object_resident(0) &&
                world.effective_cell_for_object(2)=="Highway East" &&
                world.object_resident(2),
                "Dynamic traffic ownership lost across fast-travel sector");
        pose=camera.update(1.f/120.f,target,&live,&physics);
        require(std::abs(pose.position.x-800.f)<14.f,
                "Fast travel caused camera to interpolate from previous sector");
        auto const collision=physics.sweep_sphere(
            target.eye,pose.position,settings.collision_radius,
            target.ignore_object);
        require(!collision || collision->fraction>=.99f,
                "Fast-travel camera crossed provider static mesh wall");
        require(authored.objects[0].node.id==live.objects[0].node.id &&
                serialize_scene_v3(authored)==serial,
                "World Partition move rewrote authored entity identity/data");

        for(int frame=0;frame<600;++frame) {
            auto const radians=float(frame)*.04f;
            target.velocity={60.f*std::cos(radians),0.f,60.f*std::sin(radians)};
            target.heading=radians;
            auto newpose=camera.update(1.f/120.f,target,&live,&physics);
            require(std::isfinite(newpose.position.x) &&
                    std::isfinite(newpose.position.y) &&
                    std::isfinite(newpose.position.z) &&
                    std::isfinite(newpose.roll),
                    "Long-running 120 FPS vehicle chase lost finite state");
            greatest_step=std::max(greatest_step,double(std::hypot(
                newpose.position.x-pose.position.x,
                newpose.position.y-pose.position.y,
                newpose.position.z-pose.position.z)));
            pose=newpose;
        }
        require(greatest_step<25., "Camera crossed impossible distance after fast travel");
        std::cout<<"PASS "<<physics.provider_name()<<" "
                 <<physics.provider_version()
                 <<" high-speed chase + prefetch + fast travel + 600 frames\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Camera streaming provider integration: "<<e.what()<<"\n";
        return 1;
    }
}
