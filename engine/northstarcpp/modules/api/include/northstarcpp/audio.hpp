// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <northstarcpp/audio_plugin.h>

namespace northstarcpp {

// Legacy one-stream interface. Kept ABI- and source-compatible for existing clients.
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();
    AudioOutput(AudioOutput const&) = delete;
    AudioOutput& operator=(AudioOutput const&) = delete;

    std::string provider_name() const;
    void play(NorthStarPcm16 const& pcm);
    void stop();

private:
    struct State;
    std::unique_ptr<State> state;
};

// Provider-neutral host facade. ABI 1 is mandatory; voices ABI 2 is optional.
// The selected module is pinned until this instance is destroyed, so callbacks
// and voice handles can never accidentally cross between providers.
class AudioEngine {
public:
    explicit AudioEngine(std::string preferred_provider = {});
    ~AudioEngine();
    AudioEngine(AudioEngine const&) = delete;
    AudioEngine& operator=(AudioEngine const&) = delete;

    std::string provider_name() const;
    bool supports_voices() const noexcept;

    // Always works with a valid ABI 1 provider (legacy single-stream playback).
    void play(NorthStarPcm16 const& pcm);
    void stop();

    // Optional ABI 2 API. Throws an explicit unsupported-feature error on ABI 1.
    NorthStarAudioVoiceId play_voice(NorthStarPcm16 const& pcm,
                                    NorthStarAudioVoiceOptions const& options);
    // Decode an in-memory .nac and submit one track to the active provider.
    // Callers can obtain NAC bytes through AssetManager::read(vfs_path).
    NorthStarAudioVoiceId play_nac_track(std::vector<std::uint8_t> const& nac_bytes,
                                        std::uint32_t track_id,
                                        NorthStarAudioVoiceOptions options);
    void stop_voice(NorthStarAudioVoiceId voice);
    void set_voice_gain(NorthStarAudioVoiceId voice, float gain);
    void set_voice_position(NorthStarAudioVoiceId voice, NorthStarAudioVector3 position);
    void set_listener(NorthStarAudioListener const& listener);
    void update(float delta_seconds);

private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace northstarcpp
