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
