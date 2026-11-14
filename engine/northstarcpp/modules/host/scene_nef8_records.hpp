// Private scene record contract shared by legacy NSCN codec and NEF8 ListFile.
#pragma once
#include <northstarcpp/scene.hpp>
#include <cstdint>
#include <string>
#include <vector>
namespace northstarcpp::scene_codec_detail {
enum class RecordKind : std::uint16_t {
    document=1, entity=2, layer=3, cell=4, volume=5, portal=6, plugin_component=7
};
struct Record {
    RecordKind kind;
    std::uint64_t id=0;
    std::string bytes;
    std::uint16_t version=1;
    std::uint32_t ordinal=0;
};
std::vector<Record> encode_records(Scene const& scene);
Scene decode_records(std::vector<Record> const& records);
std::string encode_nef8_scene(Scene const& scene);
Scene decode_nef8_scene(std::string const& bytes);
std::string encode_legacy_nscn(Scene const& scene);
Scene decode_legacy_nscn(std::string const& bytes);
std::string encode_nef8_chunked_scene(Scene const& scene);
Scene decode_nef8_chunked_scene(std::string const& bytes);
}
