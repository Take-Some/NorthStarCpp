#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace northstarcpp::editor::content_import {
std::vector<std::uint8_t> appendDictionary(
    std::vector<std::uint8_t> const& existing,
    std::vector<std::uint8_t> const& incoming,
    std::string const& path);
}
