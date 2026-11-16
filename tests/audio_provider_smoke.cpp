// engine.audio provider compatibility smoke: device-free.
#include <northstarcpp/audio.hpp>
#include <northstarcpp/nac.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        northstarcpp::AudioEngine audio;
        const std::string mode = argv[1];
        if (mode == "winmm") {
            if (audio.provider_name() != "WinMM" || audio.supports_voices()) return 3;
            const int16_t sample = 73;
            try {
                NorthStarAudioVoiceOptions options{sizeof(NorthStarAudioVoiceOptions),
                    1.0f, 0, 0, {0, 0, 0}};
                audio.play_voice({&sample, 1, 24000, 1}, options);
                return 4;
            } catch (std::runtime_error const&) { return 0; }
        }
        if (mode == "northstar") {
            if (audio.provider_name() != "NorthStar Audio" || !audio.supports_voices())
                return 10;
            std::vector<int16_t> tone(48000,500);
            NorthStarPcm16 pcm{tone.data(),48000,48000,1};
            NorthStarAudioListener listener{sizeof(NorthStarAudioListener),
                {0,0,0},{0,0,-1},{0,1,0}};
            audio.set_listener(listener);
            NorthStarAudioVoiceOptions options{sizeof(NorthStarAudioVoiceOptions),
                0.6f,1,1,{2,0,0}};
            auto const id=audio.play_voice(pcm,options);
            if (!id) return 11;
            audio.set_voice_gain(id,0.5f);
            audio.set_voice_position(id,{3,0,0});
            audio.update(0.016f);
            audio.stop_voice(id);
            try { audio.stop_voice(id); return 12; }
            catch (std::runtime_error const&) {}
            northstarcpp::nac::Track track;
            track.id=42; track.name="Engine"; track.sample_rate=48000;
            track.channels=1; track.samples=tone;
            std::vector<northstarcpp::nac::Track> tracks{track};
            std::vector<std::uint8_t> nac_bytes;
            std::string nac_error;
            if(!northstarcpp::nac::encode(tracks,nac_bytes,nac_error)) return 13;
            auto const nac_voice=audio.play_nac_track(nac_bytes,42,options);
            if(!nac_voice) return 14;
            audio.stop_voice(nac_voice);
            return 0;
        }
        if (mode != "fixture" || audio.provider_name() != "Fixture Audio" ||
            !audio.supports_voices()) return 5;

        const int16_t sample = 73;
        const NorthStarPcm16 pcm{&sample, 1, 24000, 1};
        audio.play(pcm);
        audio.stop();
        const NorthStarAudioListener listener{sizeof(NorthStarAudioListener),
            {0, 0, 0}, {0, 0, -1}, {0, 1, 0}};
        audio.set_listener(listener);
        const NorthStarAudioVoiceOptions options{sizeof(NorthStarAudioVoiceOptions),
            0.7f, 0, 1, {1, 2, 3}};
        const auto voice = audio.play_voice(pcm, options);
        if (voice != 73) return 6;
        audio.set_voice_position(voice, {4, 5, 6});
        audio.set_voice_gain(voice, 0.5f);
        audio.update(0.016f);
        try { audio.set_voice_gain(voice, -1.0f); return 7; }
        catch (std::invalid_argument const&) {}
        audio.stop_voice(voice);
        try { audio.stop_voice(voice); return 8; }
        catch (std::runtime_error const&) {}
        return 0;
    } catch (std::exception const& e) {
        std::cerr << e.what() << '\n';
        return 9;
    }
}
