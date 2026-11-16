#include <northstarcpp/audio.hpp>
#include <northstarcpp/ecs.hpp>
#include <northstarcpp/physics.hpp>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    using namespace northstarcpp;
    if (argc != 2)
        return 2;
    std::string const mode = argv[1];
    try {
        if (mode == "reject") {
            PhysicsScene scene(make_startup_scene());
            return 3;
        }

        if (mode == "bullet" || mode == "jolt") {
            PhysicsScene physics(make_startup_scene());
            return physics.provider_name() == (mode == "bullet" ? "Bullet" : "Jolt") ? 0 : 9;
        }

        AudioOutput audio;
        if (mode == "winmm") {
            int16_t const silence[480]{};
            audio.play({silence, 480, 24000, 1});
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            audio.stop();
            return 0;
        }
        EcsWorld ecs;
        PhysicsScene physics(make_startup_scene());
        if (mode == "default")
            return audio.provider_name() == "WinMM" && physics.provider_name() == "Jolt" &&
                ecs.provider_name() == "Flecs 4.2.0" ? 0 : 4;
        if (mode != "fixture" || audio.provider_name() != "Fixture Audio" ||
            ecs.provider_name() != "Test memory ECS" || physics.provider_name() != "Fixture Physics")
            return 5;

        int16_t const sample = 73;
        audio.play({&sample, 1, 24000, 1});
        // ABI 1 remains functional; ABI 2 is optional on the same provider.
        AudioEngine engine;
        if (!engine.supports_voices() || engine.provider_name() != "Fixture Audio")
            return 10;
        NorthStarAudioVoiceOptions options{sizeof(NorthStarAudioVoiceOptions),
            1.0f, 0u, 1u, {1.0f, 2.0f, 3.0f}};
        NorthStarAudioListener listener{sizeof(NorthStarAudioListener),
            {0, 0, 0}, {0, 0, -1}, {0, 1, 0}};
        engine.set_listener(listener);
        auto const voice = engine.play_voice({&sample, 1, 24000, 1}, options);
        if (voice != 73)
            return 11;
        engine.set_voice_gain(voice, 0.5f);
        engine.set_voice_position(voice, {3, 2, 1});
        engine.update(0.016f);
        engine.stop_voice(voice);
        try {
            engine.stop_voice(voice);
            return 12;
        } catch (std::runtime_error const&) {
            // Fixture rejects stale voice handles.
        }
        audio.stop();
        try {
            audio.play({&sample, 1, 24000, 2});
        } catch (std::runtime_error const& error) {
            return std::strstr(error.what(), "Capability fixture rejected the operation") ? 0 : 6;
        }
        return 7;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return mode == "reject" && std::strstr(error.what(), "No compatible module for engine.physics")
            ? 0 : 8;
    }
}
