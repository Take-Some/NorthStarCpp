// NorthStarCpp
// Author: Kayla Verner
#include <northstarcpp/shared_assets.hpp>
#include "asset_list_file.hpp"

#include <set>

namespace northstarcpp::shared_assets {

std::vector<Texture> load_textures(AssetStore const& assets, std::string const& path)
{
    using namespace detail;
    require(extension(path) == ".ytd", "Expected a .ytd texture dictionary");
    auto const file = read_list_file(assets, path);
    Bytes const body(file.body);
    require(file.kind == 1 && body.size() >= 64 && body.magic(0, "NETD") &&
        body.integer(4, 4) == 2 && body.integer(8, 4) == 64 &&
        body.integer(12, 4) == file.entries, "Unsupported NETD texture dictionary");

    auto const table = body.integer(16, 8);
    auto const table_size = body.integer(24, 8);
    auto const payload = body.integer(32, 8);
    auto const payload_size = body.integer(40, 8);
    require(table >= 64 && table <= payload && table_size <= payload - table &&
        table_size >= 40 && payload <= body.size() && payload_size == body.size() - payload,
        "Invalid NETD dictionary layout");
    require(body.magic(table, "NTDX") && body.integer(table + 4, 2) == 1 &&
        body.integer(table + 6, 2) == 64 && body.integer(table + 8, 4) == 32 &&
        body.integer(table + 12, 4) == file.entries, "Unsupported NTDX texture index");
    auto const mip_count = body.integer(table + 16, 4);
    auto const entries = body.integer(table + 20, 4);
    auto const mips = body.integer(table + 24, 4);
    auto const names = body.integer(table + 28, 4);
    auto const name_size = body.integer(table + 32, 4);
    require(entries >= 40 && entries <= mips && file.entries * 64 <= mips - entries &&
        mips <= names && mip_count * 32 <= names - mips &&
        names <= table_size && name_size <= table_size - names, "Invalid NTDX table ranges");

    std::vector<Texture> result;
    std::set<std::string> seen;
    std::uint64_t expected_mip = 0;
    std::uint64_t previous_end = 0;
    for (std::size_t i = 0; i < file.entries; ++i) {
        auto const entry = table + entries + i * 64;
        auto const start = body.integer(entry + 8, 8);
        auto const size = body.integer(entry + 16, 8);
        auto const name_offset = body.integer(entry + 24, 4);
        auto const length = body.integer(entry + 28, 2);
        auto const format = body.integer(entry + 30, 2);
        auto width = body.integer(entry + 32, 4);
        auto height = body.integer(entry + 36, 4);
        auto const mip_start = body.integer(entry + 40, 4);
        auto const count = body.integer(entry + 44, 4);
        auto const color_space = body.integer(entry + 48, 4);
        require(name_offset <= name_size && length <= name_size - name_offset &&
            start >= previous_end && start <= payload_size && size <= payload_size - start &&
            width > 0 && width <= 16384 && height > 0 && height <= 16384 &&
            mip_start == expected_mip && mip_start <= mip_count && count > 0 && count <= 15 &&
            count <= mip_count - mip_start && (color_space == 1 || color_space == 2),
            "Invalid NETD texture entry");
        auto name = body.string(table + names + name_offset, length);
        validate_name(name);
        require(seen.insert(name).second, "Duplicate YTD texture name");
        Texture texture{std::move(name), body.integer(entry, 8), static_cast<std::uint16_t>(format),
            color_space == 2, {}, {}};
        auto next_offset = start;
        for (std::size_t j = 0; j < count; ++j) {
            auto const mip = table + mips + (mip_start + j) * 32;
            auto const offset = body.integer(mip, 8);
            auto const length_bytes = body.integer(mip + 8, 8);
            auto const row_bytes = format == 1 ? width * 4 :
                ((width + 3) / 4) * (format == 101 || format == 102 ? 8 : 16);
            auto const rows = format == 1 ? height : (height + 3) / 4;
            require(format == 1 || (format >= 101 && format <= 104), "Unsupported NETD texture format");
            require(offset == next_offset && offset - start <= size &&
                length_bytes <= size - (offset - start) && length_bytes == row_bytes * rows &&
                body.integer(mip + 16, 4) == width && body.integer(mip + 20, 4) == height &&
                body.integer(mip + 24, 4) == j && body.integer(mip + 28, 4) == 0,
                "Invalid NETD mip layout");
            texture.mips.push_back({static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                static_cast<std::size_t>(offset - start), static_cast<std::size_t>(length_bytes)});
            next_offset += length_bytes;
            if (j + 1 < count)
                require(width > 1 || height > 1, "NETD mip chain exceeds texture dimensions");
            width = width > 1 ? width / 2 : 1;
            height = height > 1 ? height / 2 : 1;
        }
        require(next_offset == start + size, "NETD mip bytes differ from entry size");
        auto const* data = body.at(payload + start, size);
        texture.data.assign(data, data + size);
        expected_mip += count;
        previous_end = start + size;
        result.push_back(std::move(texture));
    }
    require(expected_mip == mip_count, "Unclaimed NETD mip records");
    return result;
}

} // namespace northstarcpp::shared_assets
