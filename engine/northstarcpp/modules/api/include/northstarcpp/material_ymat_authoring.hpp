// Native YMAT library editing helpers. Preserves the original NEF8 kind,
// version, header metadata and entries; replaces only decompressed body text.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
namespace northstarcpp::shared_assets {
std::vector<std::uint8_t> encode_editor_ymat(
    std::string const& authored_descriptor);
std::string decode_editor_ymat(
    std::vector<std::uint8_t> const& ymat_bytes);
struct YmatLibraryBody {
    std::uint16_t kind=0;
    std::size_t count=0;
    std::string body;
};
YmatLibraryBody read_ymat_library(
    std::vector<std::uint8_t> const& original);
std::vector<std::uint8_t> replace_ymat_library_body(
    std::vector<std::uint8_t> const& original,
    std::string const& updated_body);
} // namespace northstarcpp::shared_assets
