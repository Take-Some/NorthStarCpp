// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace northstarcpp::vfs {

inline constexpr std::uint64_t max_asset_bytes = 256ull * 1024 * 1024;
inline constexpr std::size_t max_path_bytes = 1024;

bool valid_component(std::string_view component);
void validate_path(std::string_view path, bool allow_empty);

} // namespace northstarcpp::vfs
