#include "qa_scene_fixture.hpp"
#include <northstarcpp/fps_game.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace northstarcpp;
Scene scene(bool wall)
{
    Scene result;
    qa::append(result,{"Player", {0,0,0},1.5f,0xffffff,true,2.f});
    qa::append(result,{"Target Center", {0,1,6},1.3f,0xff4040,false,0.f});
    qa::append(result,{"Target Right", {4,1,6},1.3f,0xff4040,false,0.f});
    if (wall)
        qa::append(result,{"Wall", {0,0,3},2.f,0x303030,false,0.f});
    return result;
}
int main()
{
    try {
        auto game_scene = scene(false);
        FpsArena game(game_scene);
        if (game.player_index() != 0 || game.targets() != 2 || game.hits()) return 1;
        auto const center = game.direction();
        if (std::abs(center.z - 1.f) > 0.0001f) return 2;
        auto const initial = game.eye(game_scene);
        if (std::abs(initial.y - 1.45f) > 0.0001f) return 3;
        if (!game.fire(game_scene) || game.hits() != 1 || game.fire(game_scene)) return 4;
        game.tint(game_scene);
        if (game_scene.objects[1].color != 0x3cffb5) return 5;
        game.reset();
        if (game.hits() || !game.fire(game_scene)) return 6;
        auto const world = game.movement(1.f, 1.f);
        if (std::hypot(world.x, world.z) > 1.0001f) return 7;
        game.look(100.f, -30.f);
        if (game.yaw() <= 0.f || game.pitch() <= 0.f) return 8;
        game.look(0.f, 999999.f);
        if (game.pitch() >= -1.399f) return 9;

        // Weapon recoil and magazine state: cooldown, reloading and reserve consumption.
        if (game.shots() != 1 || game.ammunition() != 11 ||
            game.recoil_amount() <= 0.f || game.health() != 100) return 13;
        if (!game.begin_reload() || !game.reloading()) return 14;
        for (int i = 0; i < 7; ++i) game.advance(0.20f, game_scene);
        if (game.reloading() || game.ammunition() != 12 ||
            game.reserve_ammunition() != 59 || game.recoil_amount() != 0.f) return 15;

        // Animation updates the hit-test/physics positions deterministically.
        Scene moving = scene(false);
        moving.objects[2].name = "Target Moving Right";
        FpsArena animated(moving);
        if (animated.moving_indices().size() != 1 || animated.targets() != 2) return 16;
        auto const initial_position = animated.moving_position(2);
        animated.advance(0.2f, moving);
        auto const changed_position = animated.moving_position(2);
        if (std::abs(changed_position.x - initial_position.x) < 0.05f) return 17;

        // Hazards damage health with an immunity interval; restart restores it.
        Scene hostile = scene(false);
        qa::append(hostile,{"Hazard Test", {0,0,0},1.f,0xe85832,false,0.f});
        FpsArena survivor(hostile);
        for (int i = 0; i < 5; ++i) {
            survivor.advance(0.20f, hostile);
            for (int j = 0; j < 4; ++j) survivor.advance(0.20f, hostile);
        }
        if (survivor.alive() || survivor.health() != 0 || survivor.fire(hostile)) return 18;
        survivor.reset();
        if (!survivor.alive() || survivor.health() != 100 ||
            survivor.ammunition() != 12 || survivor.reserve_ammunition() != 60) return 19;

        // Base blocks must not count as shootable targets.
        Scene with_base = scene(false);
        qa::append(with_base,{"Target Base Right", {0,0,5},1.f,0xf5f5f5,false,0.f});
        FpsArena base_count(with_base);
        if (base_count.targets() != 2) return 20;

        FpsArena blocked(scene(true));
        if (blocked.fire(scene(true))) return 10;

        // Actual ray direction is sampled before camera recoil kicks in.
        // A small 40m target would be missed if the post-shot camera
        // elevation were incorrectly applied to the current bullet.
        Scene precise;
        qa::append(precise,{"Player",{0,0,0},1.5f,0xffffff,true,2.f});
        qa::append(precise,{"Target Tiny",{0,1.2f,40.f},.5f,
            0xff4040,false,0.f});
        FpsArena precise_weapon(precise);
        if(!precise_weapon.fire(precise)||
           precise_weapon.hits()!=1||
           precise_weapon.pitch()<=0.f)return 21;
        // Recoil is an impulse with physically plausible spring return,
        // not a permanent modification of the user's aim pitch/yaw.
        Scene recoil_scene=scene(false);
        FpsArena spring_weapon(recoil_scene);
        auto const base_pitch=spring_weapon.pitch();
        auto const base_yaw=spring_weapon.yaw();
        spring_weapon.fire(recoil_scene);
        if(spring_weapon.pitch()<base_pitch+.025f ||
           spring_weapon.recoil_amount()<.8f)return 24;
        for(int frame=0;frame<90;++frame)
            spring_weapon.advance(1.f/60.f,recoil_scene);
        if(std::abs(spring_weapon.pitch()-base_pitch)>.001f ||
           std::abs(spring_weapon.yaw()-base_yaw)>.001f ||
           spring_weapon.recoil_amount()>0.f)return 25;
        // Debris is neither a valid target nor an arena topology error.
        Scene debris_scene=scene(false);
        FpsArena debris_weapon(debris_scene);
        qa::append(debris_scene,{"Pistol casing",{0,1.3f,2.f},
            .04f,0xd5a742,false,.008f});
        debris_scene.objects.back().node.layer="Weapon debris";
        if(!debris_weapon.fire(debris_scene)||
           debris_weapon.hits()!=1)return 22;
        debris_weapon.tint(debris_scene);
        if(debris_scene.objects.back().color!=0xd5a742)return 23;

        Scene no_player{};
        qa::append(no_player,{"Target 0", {0,1,5},1.f,0xff0000,false,0.f});
        try { FpsArena invalid(no_player); return 11; }
        catch (std::runtime_error const&) {}
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 12;
    }
}
