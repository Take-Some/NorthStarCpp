// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/audio.hpp>
#include "module_loader.hpp"

namespace northstarcpp {

struct AudioOutput::State {
    Module module{NORTHSTARCPP_CAPABILITY_AUDIO};
    NorthStarAudioApi const& api = *module.query<NorthStarAudioApi>(NORTHSTARCPP_AUDIO_ABI);
    void* audio = nullptr;

    State()
    {
        audio = api.create();
        if (!audio)
            throw module.error("Cannot create audio output");
    }

    ~State() { api.destroy(audio); }
};

AudioOutput::AudioOutput() : state(std::make_unique<State>()) {}
AudioOutput::~AudioOutput() = default;
std::string AudioOutput::provider_name() const { return state->module.provider(); }

void AudioOutput::play(NorthStarPcm16 const& pcm)
{
    if (!state->api.play(state->audio, &pcm))
        throw state->module.error("Cannot play PCM audio");
}

void AudioOutput::stop() { state->api.stop(state->audio); }

} // namespace northstarcpp
