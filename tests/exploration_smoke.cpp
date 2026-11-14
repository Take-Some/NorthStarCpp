// NorthStarCpp First Person Platform — repeatable gameplay test
#include <northstarcpp/assets.hpp>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/fps_game.hpp>
#include <northstarcpp/physics.hpp>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/shared_assets.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main(int argc, char** argv)
{
    using namespace northstarcpp;
    if (argc != 3) {
        std::cerr << "Usage: exploration_smoke SCENE_PATH ASSETS_DIR\n";
        return 2;
    }
    try {
        auto scene = load_scene(argv[1]);
        if (scene.objects.size() != 40 || !scene.objects[0].player)
            throw std::runtime_error("Exploration scene topology mismatch");

        // Existing FPS remains strict about target-based gameplay.
        bool legacy_rejected=false;
        try { FpsArena legacy(scene); }
        catch (std::runtime_error const&) { legacy_rejected=true; }
        if (!legacy_rejected)
            throw std::runtime_error("Target-range mode unexpectedly accepted an empty range");

        FpsArena explorer(scene, {}, true);
        if (explorer.targets() != 0 || explorer.won() || !explorer.alive())
            throw std::runtime_error("Exploration gameplay should not require targets");

        AssetManager assets;
        assets.mount_directory("", argv[2]);
        auto sky=assets.load_drawable_resources("models/skydome.ydd@skydome_high");
        if (sky.drawable.meshes.size()!=1 || sky.materials.empty() || sky.textures.empty())
            throw std::runtime_error("Textured skydome dependency resolution failed");

        PhysicsScene physics(scene);
        for(int i=0;i<40;++i)
            physics.step(1.f/60.f);
        auto const initial=physics.scene().objects[0].position;
        if (std::abs(initial.y)>0.08f)
            throw std::runtime_error("Player cannot stand on the platform");

        for(int i=0;i<45;++i)
            physics.step(1.f/60.f,0.f,1.f);
        auto const moved=physics.scene().objects[0].position;
        if(moved.z < initial.z+1.0f || std::abs(moved.y) > 0.10f)
            throw std::runtime_error("Player failed to walk on the platform");

        physics.jump();
        for(int i=0;i<12;++i)
            physics.step(1.f/60.f);
        if(physics.scene().objects[0].position.y < 0.15f)
            throw std::runtime_error("Grounded physics jump failed");

        auto const index=scene.objects.size()-3;
        auto before=physics.scene().objects[index].position;
        physics.apply_impulse(index, {0.f,1.3f,12.f});
        for(int i=0;i<30;++i)
            physics.step(1.f/60.f);
        if(physics.scene().objects[index].position.z < before.z + 0.20f)
            throw std::runtime_error("Dynamic crate did not respond to impulse");

        bool rejected=false;
        try { physics.apply_impulse(1,{0.f,0.f,10.f}); }
        catch(std::invalid_argument const&) { rejected=true; }
        if (!rejected)
            throw std::runtime_error("Static platform accepted a dynamic impulse");
        rejected=false;
        try { physics.apply_impulse(index,{std::numeric_limits<float>::quiet_NaN(),0.f,0.f}); }
        catch(std::invalid_argument const&) { rejected=true; }
        if(!rejected)
            throw std::runtime_error("Non-finite impulse accepted");

        CameraDirector director(load_camera_settings(assets));
        auto target=CameraTarget{};
        target.eye=explorer.eye(physics.scene());
        auto pose=director.update(1.f/60.f,target,&physics.scene(),&physics);
        if(!std::isfinite(pose.position.x) || !std::isfinite(pose.position.y) ||
           !std::isfinite(pose.position.z))
            throw std::runtime_error("First-person camera is not finite");

        std::cout << "PASS: first-person platform; " << physics.provider_name()
                  << "; sky textures=" << sky.textures.size()
                  << "; dynamic crate impulse; walking; jump\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr << "Exploration test failed: " << e.what() << '\n';
        return 1;
    }
}
