#include "qa_scene_fixture.hpp"
// Camera collision integration: same physics API under Bullet and Jolt.
#include <northstarcpp/camera.hpp>
#include <northstarcpp/physics.hpp>
#include <northstarcpp/scene.hpp>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

int main()
{
    using namespace northstarcpp;
    try {
        Scene scene;
        qa::append(scene,{"Player",{0.f,0.f,0.f},1.5f,0xffffff,true,2.f});
        PhysicsScene physics(scene);
        // Two triangles covering an independently registered static wall at Z=-3.
        // No SceneObject represents it: the collision must come from the provider
        // BVH / MeshShape, not from the camera's cube-AABB fallback.
        physics.add_static_mesh(
            {{-5.f,0.f,-3.f},{5.f,0.f,-3.f},
             {-5.f,5.f,-3.f},{5.f,5.f,-3.f}},
            {0,1,2,1,3,2});

        auto const hit = physics.sweep_sphere(
            {0.f,1.6f,0.f},{0.f,1.6f,-6.f},0.25f,0);
        if (!hit || hit->object_index!=UINT32_MAX ||
            hit->fraction<=0.20f || hit->fraction>=0.55f) {
            std::cerr<<"Triangle-mesh sphere sweep missed: "<<physics.provider_name()<<'\n';
            return 1;
        }

        auto const miss = physics.sweep_sphere(
            {0.f,1.6f,0.f},{0.f,1.6f,4.f},0.25f,0);
        if (miss) return 2;

        CameraSettings settings;
        settings.follow_smoothing=0.f;
        settings.third_person_distance=6.f;
        CameraDirector camera(settings);
        camera.set_mode(CameraMode::third_person,0.f);
        CameraTarget target;
        target.eye={0.f,1.6f,0.f};
        target.ignore_object=0;
        auto const pose=camera.update(1.f/60.f,target,&scene,&physics);
        if (pose.position.z < -3.05f || pose.position.z > -1.5f)
            return 3;

        // Rewind and verify that an object moving with the player doesn't
        // force the camera to snap to an invalid/colliding pose.
        camera.reset();
        auto const again=camera.update(1.f/60.f,target,&scene,&physics);
        if (std::abs(again.position.z-pose.position.z)>0.01f) return 4;
        try {
            physics.sweep_sphere({0,0,0},{1,0,0},-1.f);
            return 5;
        } catch(std::invalid_argument const&) {}
        try {
            physics.add_static_mesh({{0,0,0},{1,0,0},{0,1,0}}, {0,1,5});
            return 6;
        } catch(std::invalid_argument const&) {}
        std::cout << "Camera collision: " << physics.provider_name()
                  << " / " << physics.provider_version()
                  << " triangle sweep fraction " << hit->fraction << '\n';
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<e.what()<<'\n';
        return 10;
    }
}
