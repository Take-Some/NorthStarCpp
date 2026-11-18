// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/audio.hpp>
#include <northstarcpp/nac.hpp>
#include "module_loader.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

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

struct AudioEngine::State {
    // Destruction order is important: the context is destroyed while the module
    // (and its DLL) is still loaded.
    Module module;
    NorthStarAudioApi const& api;
    NorthStarAudioVoicesApi const* voices;
    void* audio = nullptr;

    explicit State(std::string preferred_provider)
        : module(NORTHSTARCPP_CAPABILITY_AUDIO, std::move(preferred_provider)),
          api(*module.query<NorthStarAudioApi>(NORTHSTARCPP_AUDIO_ABI)),
          voices(module.query_extension<NorthStarAudioVoicesApi>(
              NORTHSTARCPP_AUDIO_VOICES_ABI))
    {
        // ABI-2 is required by runtime audio: single-stream providers would
        // cut gunshots, shell impacts and overlapping music.
        if(!voices)
            throw std::runtime_error(
                "engine.audio requires multivoice provider ABI 2");
        audio = api.create();
        if (!audio)
            throw module.error("Cannot create audio engine");
    }

    ~State() { if (audio) api.destroy(audio); }
};

AudioEngine::AudioEngine(std::string preferred_provider)
    : state(std::make_unique<State>(std::move(preferred_provider))) {}
AudioEngine::~AudioEngine() = default;

std::string AudioEngine::provider_name() const { return state->module.provider(); }
bool AudioEngine::supports_voices() const noexcept { return state->voices != nullptr; }

void AudioEngine::play(NorthStarPcm16 const& pcm)
{
    if (!state->api.play(state->audio, &pcm))
        throw state->module.error("Cannot play PCM audio");
}

void AudioEngine::stop() { state->api.stop(state->audio); }

namespace {
void require_voices(NorthStarAudioVoicesApi const* api)
{
    if (!api)
        throw std::runtime_error(
            "Selected engine.audio provider does not implement optional voices ABI 2");
}
void require_gain(float gain)
{
    if (!std::isfinite(gain) || gain < 0.0f)
        throw std::invalid_argument("Audio gain must be finite and nonnegative");
}
} // namespace

NorthStarAudioVoiceId AudioEngine::play_voice(
    NorthStarPcm16 const& pcm, NorthStarAudioVoiceOptions const& options)
{
    require_voices(state->voices);
    require_gain(options.gain);
    if (options.struct_size < sizeof(NorthStarAudioVoiceOptions))
        throw std::invalid_argument("Invalid audio voice options size");
    if (!pcm.samples || !pcm.frames || !pcm.sample_rate || !pcm.channels)
        throw std::invalid_argument("Invalid PCM audio data");
    auto const id = state->voices->play_voice(state->audio, &pcm, &options);
    if (!id)
        throw state->module.error("Cannot start audio voice");
    return id;
}

NorthStarAudioVoiceId AudioEngine::play_nac_track(
    std::vector<std::uint8_t> const& nac_bytes, std::uint32_t track_id,
    NorthStarAudioVoiceOptions options)
{
    require_voices(state->voices);
    std::vector<nac::Track> tracks;
    std::string error;
    if (!nac::decode(nac_bytes, tracks, error))
        throw std::runtime_error("Cannot decode NAC: " + error);
    for (auto const& track : tracks) {
        if (track.id != track_id) continue;
        auto const frames = track.samples.size() / track.channels;
        if (track.looping && (track.loop_start != 0 ||
            (track.loop_end != 0 && track.loop_end != frames)))
            throw std::runtime_error("NAC custom loop regions require a newer audio ABI");
        if (track.looping) options.looping = 1;
        NorthStarPcm16 pcm{track.samples.data(),
            static_cast<std::uint32_t>(frames),track.sample_rate,track.channels};
        return play_voice(pcm, options);
    }
    throw std::runtime_error("NAC track ID not found");
}

void AudioEngine::stop_voice(NorthStarAudioVoiceId voice)
{
    require_voices(state->voices);
    if (!voice || !state->voices->stop_voice(state->audio, voice))
        throw state->module.error("Cannot stop audio voice");
}

void AudioEngine::set_voice_gain(NorthStarAudioVoiceId voice, float gain)
{
    require_voices(state->voices);
    require_gain(gain);
    if (!voice || !state->voices->set_voice_gain(state->audio, voice, gain))
        throw state->module.error("Cannot change audio voice gain");
}

void AudioEngine::set_voice_position(NorthStarAudioVoiceId voice,
                                     NorthStarAudioVector3 position)
{
    require_voices(state->voices);
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        throw std::invalid_argument("Invalid audio voice position");
    if (!voice || !state->voices->set_voice_position(state->audio, voice, position))
        throw state->module.error("Cannot change audio voice position");
}

void AudioEngine::set_listener(NorthStarAudioListener const& listener)
{
    require_voices(state->voices);
    if (listener.struct_size < sizeof(NorthStarAudioListener))
        throw std::invalid_argument("Invalid audio listener size");
    if (!state->voices->set_listener(state->audio, &listener))
        throw state->module.error("Cannot update audio listener");
}

void AudioEngine::update(float delta_seconds)
{
    require_voices(state->voices);
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0f)
        throw std::invalid_argument("Audio update delta must be finite and nonnegative");
    state->voices->update(state->audio, delta_seconds);
}

} // namespace northstarcpp
