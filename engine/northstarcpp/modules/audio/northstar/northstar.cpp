// NorthStar Audio: replaceable first-party C++ engine.audio provider.
// Implements the legacy PCM ABI 1 and optional multivoice ABI 2.
#include <northstarcpp/audio_plugin.h>
#include "module_provider.hpp"
#include "native_mixer.hpp"
#include "native_device.hpp"
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace {
using namespace northstarcpp::native_audio;
using namespace northstarcpp::module_provider;

struct Output {
    Mixer mixer;
    std::mutex mutex;
    Device device{mixer,mutex}; // destroyed first; never refers to destroyed mixer
};

Output& checked(void* context) {
    if (!context) throw std::invalid_argument("Missing NorthStar audio context");
    return *static_cast<Output*>(context);
}
void* create() {
    Output* result=nullptr;
    guarded([&]{ result=new Output; });
    return result;
}
void destroy(void* p) { delete static_cast<Output*>(p); }

int play(void* p, NorthStarPcm16 const* pcm) {
    return guarded([&] {
        if (!pcm) throw std::invalid_argument("Missing PCM audio");
        auto& out=checked(p);
        std::lock_guard<std::mutex> lock(out.mutex);
        NorthStarAudioVoiceOptions options{sizeof(NorthStarAudioVoiceOptions),
                                           1.0f,0,0,{0,0,0}};
        out.mixer.stop_all();
        out.mixer.play(*pcm,options);
    });
}
void stop(void* p) {
    if (!p) return;
    auto& out=*static_cast<Output*>(p);
    std::lock_guard<std::mutex> lock(out.mutex);
    out.mixer.stop_all();
}
NorthStarAudioVoiceId play_voice(void* p, NorthStarPcm16 const* pcm,
                                 NorthStarAudioVoiceOptions const* options) {
    NorthStarAudioVoiceId id=0;
    guarded([&] {
        if (!pcm || !options) throw std::invalid_argument("Missing PCM or voice options");
        auto& out=checked(p);
        std::lock_guard<std::mutex> lock(out.mutex);
        id=out.mixer.play(*pcm,*options);
    });
    return id;
}
int stop_voice(void* p, NorthStarAudioVoiceId id) {
    return guarded([&] {
        auto& out=checked(p);
        std::lock_guard<std::mutex> lock(out.mutex);
        if (!id || !out.mixer.stop(id)) throw std::runtime_error("Unknown audio voice");
    });
}
int gain(void* p, NorthStarAudioVoiceId id, float value) {
    return guarded([&] {
        auto& out=checked(p);
        std::lock_guard<std::mutex> lock(out.mutex);
        if (!id || !out.mixer.gain(id,value)) throw std::runtime_error("Invalid voice gain/id");
    });
}
int position(void* p, NorthStarAudioVoiceId id, NorthStarAudioVector3 value) {
    return guarded([&] {
        auto& out=checked(p);
        std::lock_guard<std::mutex> lock(out.mutex);
        if (!id || !out.mixer.position(id,value)) throw std::runtime_error("Invalid voice position/id");
    });
}
int listener(void* p, NorthStarAudioListener const* value) {
    return guarded([&] {
        if (!value) throw std::invalid_argument("Missing audio listener");
        auto& out=checked(p);
        std::lock_guard<std::mutex> lock(out.mutex);
        if (!out.mixer.listener(*value)) throw std::runtime_error("Invalid listener orientation");
    });
}
void update(void*, float) { /* Device thread drives the audio clock independently. */ }

NorthStarAudioApi const api{create,destroy,play,stop};
NorthStarAudioVoicesApi const voices{
    NORTHSTARCPP_AUDIO_VOICES_ABI,sizeof(NorthStarAudioVoicesApi),
    play_voice,stop_voice,gain,position,listener,update
};
NorthStarModule const descriptors[]{
    {sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_AUDIO,
     "NorthStar Audio","0.1.0",NORTHSTARCPP_AUDIO_ABI,sizeof(api),&api,last_error},
    {sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_AUDIO,
     "NorthStar Audio","0.1.0",NORTHSTARCPP_AUDIO_VOICES_ABI,sizeof(voices),&voices,last_error}
};
} // namespace

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarModule const* northstar_module_query(uint32_t host, char const* capability,
                                              uint32_t version) {
    for (auto const& module : descriptors)
        if (auto const* result=match(module,host,capability,version)) return result;
    return nullptr;
}
