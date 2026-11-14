// NorthStarCpp FPS runtime content loaded through engine.assets ABI.
// Copyright (c) 2026 Kayla's Systems - Kayla Verner
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace northstarcpp {
class AssetStore;

struct FpsTuning {
    int magazine = 12;
    int reserve = 60;
    int hazard_damage = 20;
    float fire_interval = 0.16f;
    float reload_seconds = 1.25f;
    float weapon_range = 55.f;
    std::uint32_t hit_color = 0x3cffb5u;
};

struct FpsAudioClip {
    std::vector<std::int16_t> samples;
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;
    std::uint32_t frames() const { return channels ? static_cast<std::uint32_t>(samples.size() / channels) : 0; }
};

struct FpsContent {
    FpsTuning tuning;
    FpsAudioClip shot;
    FpsAudioClip reload;
};

FpsTuning parse_fps_tuning(std::string const& json);
FpsAudioClip decode_fps_wav(std::vector<std::uint8_t> const& bytes);
// Load the entire game content via the active, replaceable engine.assets provider.
// Mount either the project's assets/ or a packaged game's game/assets/ at VFS root.
FpsContent load_fps_content(AssetStore const& store);
} // namespace northstarcpp
