// Single canonical YMAT: NEF8 kind 4, XML material-library payload.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
namespace northstarcpp::shared_assets {
constexpr std::uint16_t canonical_ymat_kind=4;
std::vector<std::uint8_t> encode_xml_ymat(
    std::string const& xml,std::size_t materials);
std::vector<std::uint8_t> migrate_ymat_to_xml(
    std::vector<std::uint8_t> const& source);
bool is_xml_ymat(std::vector<std::uint8_t> const& source);
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
} // namespace northstarcpp::shared_assets
