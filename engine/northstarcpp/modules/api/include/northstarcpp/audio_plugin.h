// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once
#include <northstarcpp/module.h>

#define NORTHSTARCPP_AUDIO_ABI 1u

typedef struct NorthStarPcm16 {
    const int16_t* samples; // interleaved; valid for the duration of play()
    uint32_t frames;
    uint32_t sample_rate;
    uint32_t channels;
} NorthStarPcm16;

typedef struct NorthStarAudioApi {
    void* (*create)(void);
    void (*destroy)(void* audio);
    int (*play)(void* audio, const NorthStarPcm16* pcm);
    void (*stop)(void* audio);
} NorthStarAudioApi;

// Optional engine.audio extension, queried on the SAME provider/context as ABI 1.
// ABI 1 is unchanged: old providers remain valid and simply lack voices support.
// All PCM sample data must be copied or consumed before play_voice returns;
// providers must not retain the caller's pointer. No exceptions cross the C ABI.
#define NORTHSTARCPP_AUDIO_VOICES_ABI 2u

typedef uint64_t NorthStarAudioVoiceId; // 0 is invalid, scoped to the audio context

typedef struct NorthStarAudioVector3 {
    float x, y, z;
} NorthStarAudioVector3;

typedef struct NorthStarAudioVoiceOptions {
    uint32_t struct_size;
    float gain;             // linear gain, 0 = silent, 1 = unchanged
    uint32_t looping;       // 0 = one shot, non-zero = loop
    uint32_t spatial;       // 0 = 2D, non-zero = 3D
    NorthStarAudioVector3 position;
} NorthStarAudioVoiceOptions;

typedef struct NorthStarAudioListener {
    uint32_t struct_size;
    NorthStarAudioVector3 position;
    NorthStarAudioVector3 forward;
    NorthStarAudioVector3 up;
} NorthStarAudioListener;

typedef struct NorthStarAudioVoicesApi {
    uint32_t abi_version;   // NORTHSTARCPP_AUDIO_VOICES_ABI
    uint32_t struct_size;   // sizeof(NorthStarAudioVoicesApi)
    // Providers own returned voice IDs; zero means failure (consult last_error).
    NorthStarAudioVoiceId (*play_voice)(void* audio, const NorthStarPcm16* pcm,
                                       const NorthStarAudioVoiceOptions* options);
    int (*stop_voice)(void* audio, NorthStarAudioVoiceId voice);
    int (*set_voice_gain)(void* audio, NorthStarAudioVoiceId voice, float gain);
    int (*set_voice_position)(void* audio, NorthStarAudioVoiceId voice,
                              NorthStarAudioVector3 position);
    int (*set_listener)(void* audio, const NorthStarAudioListener* listener);
    void (*update)(void* audio, float delta_seconds);
} NorthStarAudioVoicesApi;
