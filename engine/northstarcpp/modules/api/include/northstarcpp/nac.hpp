// NorthStar Audio Container (.nac) - public provider-neutral file format.
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace northstarcpp::nac {
inline constexpr std::uint16_t version = 1;
inline constexpr std::uint32_t codec_pcm16 = 1;
// The NEF8 asset reader permits up to 256 MiB on disk. Audio can also
// expand to 256 MiB of PCM under the same v1 envelope.
inline constexpr std::size_t max_container_bytes = 256u * 1024 * 1024;
inline constexpr std::size_t max_pcm_bytes = 256u * 1024 * 1024;

// V1 stores interleaved little-endian PCM16, CRC32 and optional loop markers.
struct Track {
    std::uint32_t id = 0;
    std::string name;
    std::uint32_t sample_rate = 48000;
    std::uint16_t channels = 2;
    bool looping = false;
    std::uint64_t loop_start = 0;
    std::uint64_t loop_end = 0;
    std::vector<std::int16_t> samples;
};

// Metadata-only index of NEF8 type 12. Inspects the header and audio metadata,
// without decompressing PCM data or loading the entire container.
struct TrackInfo {
    std::uint32_t id = 0;
    std::string name;
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;
    std::uint64_t frames = 0;
    std::uint64_t payload_bytes = 0;
    bool looping = false;
};
bool inspect_file(std::filesystem::path const& file,
                  std::vector<TrackInfo>& tracks, std::string& error);

// Binary layout documented in docs/NAC_FORMAT_V1.md.
// All APIs fail atomically on invalid input.
bool encode(std::vector<Track> const& tracks,
            std::vector<std::uint8_t>& bytes, std::string& error);
bool decode(std::uint8_t const* bytes, std::size_t size,
            std::vector<Track>& tracks, std::string& error);
inline bool decode(std::vector<std::uint8_t> const& bytes,
                   std::vector<Track>& tracks, std::string& error) {
    return decode(bytes.data(), bytes.size(), tracks, error);
}
} // namespace northstarcpp::nac
