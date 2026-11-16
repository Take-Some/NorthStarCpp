#include "native_mixer.hpp"
#include <array>
#include <cstdint>
#include <vector>

int main() {
    using namespace northstarcpp::native_audio;
    Mixer mixer;
    std::vector<std::int16_t> tone(256,5000);
    NorthStarPcm16 pcm{tone.data(),256,48000,1};
    NorthStarAudioVoiceOptions normal{sizeof(NorthStarAudioVoiceOptions),
                                     1.f,0,0,{0,0,0}};
    auto a=mixer.play(pcm,normal);
    auto b=mixer.play(pcm,normal);
    if(!a || !b || a==b || mixer.voice_count()!=2) return 1;
    std::array<std::int16_t,block_frames*2> output{};
    mixer.mix(output.data(),block_frames);
    if(output[0]<9000 || output[0]>11000 || output[0]!=output[1] ||
       mixer.voice_count()!=0 || mixer.stop(a)) return 2;

    NorthStarAudioVoiceOptions position{sizeof(NorthStarAudioVoiceOptions),
                                       1.f,1,1,{10,0,0}};
    auto spatial=mixer.play(pcm,position);
    if(!mixer.gain(spatial,0.8f) || mixer.gain(spatial,-1.f)) return 3;
    NorthStarAudioListener listen{sizeof(NorthStarAudioListener),
        {0,0,0},{0,0,-1},{0,1,0}};
    if(!mixer.listener(listen)) return 4;
    mixer.mix(output.data(),block_frames);
    if(output[1] <= output[0] || mixer.voice_count()!=1) return 5;
    if(!mixer.position(spatial,{0,0,10}) || !mixer.stop(spatial) ||
        mixer.stop(spatial) || mixer.voice_count()!=0) return 6;

    // Linear resampling 24k->48k: a 256 frame clip lasts 512 output frames.
    NorthStarPcm16 slower{tone.data(),256,24000,1};
    auto resampled=mixer.play(slower,normal);
    mixer.mix(output.data(),block_frames);
    if(mixer.voice_count()!=1) return 7;
    mixer.mix(output.data(),block_frames);
    if(mixer.voice_count()!=0 || mixer.stop(resampled)) return 8;

    // Explicit voice budget with deterministic oldest-first replacement.
    for(std::size_t i=0;i<max_voices+1;++i) {
        auto id=mixer.play(pcm,position);
        if(id==0) return 9;
    }
    if(mixer.voice_count()!=max_voices) return 10;
    return 0;
}
