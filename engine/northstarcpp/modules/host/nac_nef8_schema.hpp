#pragma once
// NAC is a typed NEF8 ListFile. The outer envelope is owned by engine.assets.
#include <northstarcpp/nac.hpp>
#include "asset_list_file.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace northstarcpp::nac::schema {
constexpr std::uint16_t nef8_kind_audio=12;
constexpr std::size_t max_tracks=1024;
constexpr std::uint64_t max_body=64ull*1024*1024;
constexpr std::size_t nef8_header=128;
inline std::uint64_t align16(std::uint64_t n) {return (n+15u)&~15ull;}
struct Record {
    TrackInfo info;
    std::uint64_t offset=0;
    std::uint64_t loop_start=0,loop_end=0;
    std::uint32_t checksum=0;
};
std::string serialize_metadata(std::vector<Record> const& records);
std::vector<Record> parse_metadata(
    shared_assets::detail::Tree const& metadata,
    std::uint64_t expanded_bytes,std::uint64_t expected_count);
} // namespace northstarcpp::nac::schema
