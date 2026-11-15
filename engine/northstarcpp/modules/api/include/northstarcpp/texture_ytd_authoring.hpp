#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace northstarcpp::shared_assets {
// Build a standards-compliant single-texture NEF8/NETD/YTD from RGBA8
// editor import data. The native YTD remains the runtime texture resource.
std::vector<std::uint8_t> encode_rgba8_ytd(
    std::string const& name,unsigned width,unsigned height,
    std::vector<std::uint8_t> const& pixels,bool srgb=true);
}
