#pragma once
#include <northstarcpp/nac.hpp>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <zlib.h>

namespace northstarcpp::nac::wire {
// NAC payload is inside a shared NEF8 envelope (no private NAC1 header).
inline std::uint64_t align16(std::uint64_t n) { return (n + 15) & ~15ull; }
inline std::uint64_t read(std::uint8_t const* p, int size) {
    std::uint64_t n = 0;
    for (int i = 0; i < size; ++i) n |= std::uint64_t(p[i]) << (i * 8);
    return n;
}
inline void write(std::uint8_t* p, std::uint64_t n, int size) {
    for (int i = 0; i < size; ++i) p[i] = std::uint8_t(n >> (i * 8));
}
inline std::uint32_t crc(std::uint8_t const* p, std::size_t size) {
    return static_cast<std::uint32_t>(::crc32(0,p,static_cast<uInt>(size)));
}
inline void validate(Track const& t) {
    if (t.name.empty() || t.name.size() > 47 ||
        t.name.find('\0') != std::string::npos)
        throw std::runtime_error("NAC invalid track name");
    if (t.sample_rate < 8000 || t.sample_rate > 192000 ||
        (t.channels != 1 && t.channels != 2))
        throw std::runtime_error("NAC supports PCM16 mono/stereo 8..192 kHz");
    if (t.samples.empty() || t.samples.size() % t.channels ||
        t.samples.size() > max_pcm_bytes / sizeof(std::int16_t))
        throw std::runtime_error("NAC invalid sample count");
    auto frames = t.samples.size() / t.channels;
    if (frames > 0x7fffffffu)
        throw std::runtime_error("NAC too many PCM frames");
    if (t.looping) {
        auto end = t.loop_end ? t.loop_end : frames;
        if (!(t.loop_start < end && end <= frames))
            throw std::runtime_error("NAC invalid loop range");
    } else if (t.loop_start || t.loop_end)
        throw std::runtime_error("NAC loop range without loop flag");
}
} // namespace northstarcpp::nac::wire
