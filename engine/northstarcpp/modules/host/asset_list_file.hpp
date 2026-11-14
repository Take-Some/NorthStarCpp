// NorthStarCpp
// Author: Kayla Verner
#pragma once

#include <boost/property_tree/ptree.hpp>
#include <northstarcpp/shared_assets.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace northstarcpp {
class AssetStore;

namespace shared_assets::detail {

using Tree = boost::property_tree::ptree;

class Bytes {
public:
    explicit Bytes(std::vector<std::uint8_t> const& bytes) : data_(bytes.data()), size_(bytes.size()) {}
    std::uint8_t const* at(std::uint64_t offset, std::uint64_t length) const;
    std::uint64_t integer(std::uint64_t offset, unsigned width) const;
    std::string string(std::uint64_t offset, std::uint64_t length) const;
    bool magic(std::uint64_t offset, std::string_view value) const;
    float real(std::uint64_t offset) const;
    std::size_t size() const { return size_; }
private:
    std::uint8_t const* data_;
    std::size_t size_;
};

void require(bool condition, char const* message);
std::string extension(std::string const& path);
void validate_name(std::string const& name);
void validate_reference(std::string const& reference);

struct ListFile {
    std::uint16_t kind;
    std::size_t entries;
    Tree metadata;
    std::vector<std::uint8_t> body;
};

ListFile read_list_file(AssetStore const& assets, std::string const& path);
ListFile parse_list_file(std::vector<std::uint8_t> const& bytes);
Tree parse_json(std::string const& text);
bool legacy_material_library(ListFile const& file);
std::vector<Material> parse_materials(ListFile const& file, std::string const& path);

} // namespace shared_assets::detail
} // namespace northstarcpp
