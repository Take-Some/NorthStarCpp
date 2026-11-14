#include <northstarcpp/fps_assets.hpp>
#include <northstarcpp/fps_game.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/camera_track.hpp>
#include <iostream>
#include <stdexcept>
using namespace northstarcpp;
int main(int argc,char** argv)
{
    try {
        if (argc != 2) return 2;
        AssetStore store;
        store.mount_directory("", argv[1]);
        FpsContent const content = load_fps_content(store);
        CameraTrack const intro=load_camera_track(store,"cinematics/intro.camera.json");
        if (intro.size()!=3 || intro.duration()!=4.f ||
            intro.sample(2.f).fov_y_degrees < 50.f)
            return 16;
        auto const& w = content.tuning;
        CameraSettings const camera=load_camera_settings(store);
        if (camera.default_fov_y!=78.f || camera.aimed_fov_y!=55.f ||
            camera.third_person_distance!=4.2f || camera.near_plane!=0.08f)
            return 14;
        try {
            (void)parse_camera_settings(R"({"schema":1,"projection":{"near":-1}})");
            return 15;
        } catch(std::invalid_argument const&) {}

        if (w.magazine != 18 || w.reserve != 108 ||
            w.hazard_damage != 15 ||
            w.fire_interval < 0.129f || w.fire_interval > 0.131f ||
            w.hit_color != 0x40F0ADu ||
            content.shot.sample_rate != 24000 ||
            content.shot.channels != 1 || content.shot.frames() < 200 ||
            content.reload.frames() < 200) return 3;
        Scene scene;
        scene.objects.push_back({"Player", {0,0,0},1.5f,0xffffff,true,2.f});
        scene.objects.push_back({"Target Center", {0,1,6},1.3f,0xff4040,false,0.f});
        FpsArena game(scene,w);
        if (game.ammunition() != 18 || game.reserve_ammunition() != 108 || game.magazine_size()!=18) return 4;
        if (!game.fire(scene) || game.ammunition() != 17) return 5;
        game.tint(scene);
        if (scene.objects[1].color != 0x40F0ADu) return 6;
        if (!game.begin_reload()) return 7;
        for (int i=0; i<10; ++i) game.advance(0.2f,scene);
        if (game.ammunition() != 18 || game.reserve_ammunition() != 107) return 8;
        game.reset();
        if (game.ammunition() != 18 || game.reserve_ammunition() != 108) return 9;
        for (auto invalid : {
             R"({"schema":1,"weapon":{"magazine":0,"reserve":10,"fire_interval":0.13,"reload_seconds":1,"range":20},"arena":{"hazard_damage":20,"hit_color":"#fff"}})",
             R"({"schema":2})"}) {
            try { (void)parse_fps_tuning(invalid); return 10; }
            catch (std::exception const&) {}
        }
        try { (void)decode_fps_wav({0,1,2}); return 11; }
        catch (std::exception const&) {}
        std::cout << "FPS assets via engine.assets: " << store.provider_name() << "\n";
        return 0;
    } catch (std::exception const& e) {
        std::cerr << e.what() << "\n";
        return 12;
    }
}
