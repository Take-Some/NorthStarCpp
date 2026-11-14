// NorthStarCpp FPS AssetManager-backed game content.
#include <northstarcpp/fps_assets.hpp>
#include <northstarcpp/assets.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace northstarcpp {
namespace {
std::uint16_t u16(std::uint8_t const* p) {
    return static_cast<std::uint16_t>(p[0] | (std::uint16_t(p[1]) << 8));
}
std::uint32_t u32(std::uint8_t const* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
void valid(bool predicate, char const* message) {
    if (!predicate) throw std::runtime_error(message);
}
} // namespace

FpsTuning parse_fps_tuning(std::string const& json)
{
    valid(json.size() <= 32768, "FPS configuration exceeds 32 KiB");
    boost::property_tree::ptree cfg;
    std::istringstream input(json);
    boost::property_tree::read_json(input, cfg);
    valid(cfg.get<int>("schema") == 1, "Unsupported FPS configuration schema");
    FpsTuning result;
    result.magazine = cfg.get<int>("weapon.magazine");
    result.reserve = cfg.get<int>("weapon.reserve");
    result.fire_interval = cfg.get<float>("weapon.fire_interval");
    result.reload_seconds = cfg.get<float>("weapon.reload_seconds");
    result.weapon_range = cfg.get<float>("weapon.range");
    result.hazard_damage = cfg.get<int>("arena.hazard_damage");
    std::string color = cfg.get<std::string>("arena.hit_color");
    valid(color.size() == 7 && color[0] == '#', "Expected #RRGGBB hit_color");
    unsigned value = 0;
    for (std::size_t i = 1; i < color.size(); ++i) {
        char c = color[i];
        unsigned digit = c >= '0' && c <= '9' ? static_cast<unsigned>(c - '0') :
             c >= 'a' && c <= 'f' ? static_cast<unsigned>(c - 'a' + 10) :
             c >= 'A' && c <= 'F' ? static_cast<unsigned>(c - 'A' + 10) : 255u;
        valid(digit <= 15u, "Invalid hit_color hex value");
        value = (value << 4) | digit;
    }
    result.hit_color = value;
    valid(result.magazine >= 1 && result.magazine <= 100 &&
          result.reserve >= 0 && result.reserve <= 10000 &&
          result.hazard_damage >= 1 && result.hazard_damage <= 100 &&
          std::isfinite(result.fire_interval) && result.fire_interval >= 0.04f &&
          result.fire_interval <= 2.f &&
          std::isfinite(result.reload_seconds) && result.reload_seconds >= 0.1f &&
          result.reload_seconds <= 10.f &&
          std::isfinite(result.weapon_range) && result.weapon_range >= 1.f &&
          result.weapon_range <= 200.f, "FPS gameplay configuration outside allowed range");
    return result;
}

FpsAudioClip decode_fps_wav(std::vector<std::uint8_t> const& data)
{
    valid(data.size() >= 44 && data.size() <= 8 * 1024 * 1024, "Invalid FPS WAV size");
    auto const* b = data.data();
    valid(std::memcmp(b,"RIFF",4)==0 && std::memcmp(b+8,"WAVE",4)==0,
          "FPS sound must be RIFF/WAVE");
    valid(std::uint64_t(u32(b+4)) + 8 <= data.size(), "Truncated RIFF/WAVE payload");
    FpsAudioClip result;
    bool fmt = false;
    bool pcm = false;
    std::size_t position = 12;
    while (position + 8 <= data.size()) {
        std::uint32_t const length = u32(b + position + 4);
        std::size_t const offset = position + 8;
        valid(length <= data.size() - offset, "Truncated FPS WAV chunk");
        if (std::memcmp(b + position, "fmt ", 4) == 0) {
            valid(length >= 16, "Invalid WAV fmt chunk");
            auto const format = u16(b+offset);
            result.channels = u16(b+offset+2);
            result.sample_rate = u32(b+offset+4);
            auto const block = u16(b+offset+12);
            auto const bits = u16(b+offset+14);
            valid(format == 1 && bits == 16 &&
                  (result.channels == 1 || result.channels == 2) &&
                  result.sample_rate >= 8000 && result.sample_rate <= 192000 &&
                  block == result.channels * 2, "Only PCM16 mono/stereo WAV is supported");
            fmt = true;
        } else if (std::memcmp(b + position, "data", 4) == 0) {
            valid(length > 0 && length % 2 == 0, "Invalid WAV PCM payload");
            result.samples.resize(length/2);
            for (std::size_t j=0;j<result.samples.size();++j)
                result.samples[j] = static_cast<std::int16_t>(u16(b + offset + 2*j));
            pcm = true;
        }
        position = offset + length + (length & 1u);
    }
    valid(fmt && pcm && result.samples.size() % result.channels == 0 &&
          result.frames() <= result.sample_rate * 10u,
          "Unsupported or incomplete FPS WAV sound");
    return result;
}

FpsContent load_fps_content(AssetStore const& assets)
{
    FpsContent content;
    auto const config = assets.read("gameplay/fps.json");
    content.tuning = parse_fps_tuning(std::string(config.begin(),config.end()));
    content.shot = decode_fps_wav(assets.read("audio/shot.wav"));
    content.reload = decode_fps_wav(assets.read("audio/reload.wav"));
    return content;
}
} // namespace northstarcpp
