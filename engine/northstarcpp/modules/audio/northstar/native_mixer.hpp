// First-party NorthStar Audio software mixer; no OS or third-party dependencies.
#pragma once
#include <northstarcpp/audio_plugin.h>
#include <array>
#include <cstdint>
#include <vector>

namespace northstarcpp::native_audio {
inline constexpr std::uint32_t sample_rate = 48000;
inline constexpr std::uint32_t block_frames = 256;
inline constexpr std::size_t max_voices = 128;

// Not internally synchronized: owning provider guards state with a mutex.
class Mixer {
public:
    struct PreparedPcm {
        std::vector<std::int16_t> samples;
        std::uint32_t frames=0,rate=0,channels=0;
    };
    // Allocate and copy decoded PCM on the gameplay thread BEFORE locking
    // the device mixer. A large NAC clip must not block real-time output.
    static PreparedPcm prepare(NorthStarPcm16 const&);
    std::uint64_t play(PreparedPcm&&,NorthStarAudioVoiceOptions const&);
    std::uint64_t play(NorthStarPcm16 const&, NorthStarAudioVoiceOptions const&);
    bool stop(std::uint64_t id);
    void stop_all() noexcept;
    bool gain(std::uint64_t id, float gain);
    bool position(std::uint64_t id, NorthStarAudioVector3 position);
    bool listener(NorthStarAudioListener const&);
    void mix(std::int16_t* interleaved_stereo, std::uint32_t frames);
    std::size_t voice_count() const noexcept { return voices_.size(); }

private:
    struct Voice {
        std::uint64_t id;
        std::vector<std::int16_t> pcm;
        std::uint32_t rate, channels, frames;
        std::uint64_t loop_start = 0;
        double cursor = 0.0;
        float gain = 1.0f;
        bool looping = false, spatial = false;
        NorthStarAudioVector3 pos{};
    };
    Voice* find(std::uint64_t id);
    std::vector<Voice> voices_;
    std::uint64_t next_id_ = 1;
    // Block peak limiter state, shared across output channels to preserve
    // stereo imaging and avoid the crackle of hard PCM16 clipping.
    float output_gain_ = 1.f;
    NorthStarAudioListener listener_{sizeof(NorthStarAudioListener),
        {0, 0, 0}, {0, 0, -1}, {0, 1, 0}};
};
} // namespace northstarcpp::native_audio
