// NorthStarCpp
// Author: Kayla Verner
#include <northstarcpp/shared_assets.hpp>
#include "asset_list_file.hpp"

#include <set>
#include <map>
#include <algorithm>

namespace northstarcpp::shared_assets {

// Earlier NEF8 importers wrote some DXT5/BC3 blocks with a BC7 (104)
// format tag. BC3 and BC7 are both 16 bytes per 4x4 tile, so structural
// size validation cannot distinguish them. The wrong BC7 decode produces
// purple/red block artifacts instead of the original color image.
//
// This bounded high-confidence check recognizes the classic BC3 alpha plane
// (2 endpoints + 48 index bits) when it occupies most blocks, and requires
// a substantial number of invalid BC7 mode prefixes plus implausibly
// many mode-0 blocks. Ordinary BC7, including black reserved blocks, is
// preserved. No filename-based special cases and no mutation of ListFiles.
static bool legacy_bc3_mislabeled_bc7(Texture const& texture) {
    if(texture.format_id!=104 || texture.mips.empty())return false;
    auto const& mip=texture.mips.front();
    if(mip.offset>texture.data.size() || mip.size>texture.data.size()-mip.offset)
        return false;
    auto const blocks=std::min<std::size_t>(mip.size/16,2048);
    if(blocks<32)return false;
    std::size_t invalidBC7=0,bc7ModeZero=0,constantBC3Alpha=0;
    std::size_t legacyBC3Alpha=0;
    auto const* data=texture.data.data()+mip.offset;
    for(std::size_t i=0;i<blocks;++i) {
        auto const* block=data+16*i;
        if(block[0]==0)++invalidBC7;
        if(block[0]&1)++bc7ModeZero;
        // BC3 encoding of uniform alpha: a0 == a1 == 255 or
        // a0=0,a1=1 with all 3-bit indices zero.
        bool const alphaHeader=
            (block[0]==255 && block[1]==255) ||
            (block[0]==0 && block[1]==1);
        bool alphaIndicesZero=true;
        for(int j=2;j<8;++j)alphaIndicesZero &= block[j]==0;
        if(alphaHeader && alphaIndicesZero)++constantBC3Alpha;
        // Legacy character BC3 uses a near-transparent 0/5 alpha ramp with
        // fully saturated selectors. Both 16-byte BC3/BC7 block formats are
        // structurally identical, but a zero first byte is invalid for BC7.
        // Require 95% identical alpha-plane signatures; never infer from
        // the texture filename or from a single black BC7 block.
        bool saturatedIndices=true;
        for(int j=2;j<8;++j)saturatedIndices &= block[j]==255;
        if(block[0]==0 && block[1]==5 && saturatedIndices)++legacyBC3Alpha;
    }
    return (invalidBC7*8>=blocks && bc7ModeZero*2>=blocks &&
            constantBC3Alpha*3>=blocks*2) ||
           (invalidBC7*20>=blocks*19 && legacyBC3Alpha*20>=blocks*19);
}

static std::vector<Texture> parse_texture_dictionary(
    detail::ListFile const& file, std::string const& path)
{
    using namespace detail;
    require(extension(path) == ".ytd", "Expected a .ytd texture dictionary");
    Bytes const body(file.body);
    require((file.kind == 1 || file.kind == 5) && body.size() >= 64 && body.magic(0, "NETD") &&
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
    // Validate the actual mip byte ranges globally. Some imported YTDs
    // reuse identical tail blocks while their declared per-texture spans
    // overlap, so comparing outer spans would reject valid dictionaries.
    std::map<std::uint64_t,std::uint64_t> occupied_mip_ranges;
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
            start <= payload_size && size <= payload_size - start &&
            width > 0 && width <= 16384 && height > 0 && height <= 16384 &&
            mip_start == expected_mip && mip_start <= mip_count && count > 0 && count <= 15 &&
            count <= mip_count - mip_start && (color_space == 1 || color_space == 2),
            "Invalid NETD texture entry");
        require(size > 0, "Empty NETD payload range");
        auto name = body.string(table + names + name_offset, length);
        validate_name(name);
        require(seen.insert(name).second, "Duplicate YTD texture name");
        Texture texture{std::move(name), body.integer(entry, 8), static_cast<std::uint16_t>(format),
            color_space == 2, {}, {}};
        std::uint64_t first_mip=payload_size;
        std::uint64_t last_mip=0;
        // Build a tightly packed per-texture copy. Imported dictionaries may
        // reference shared blocks far away from the first mip; retaining the
        // entire outer span wastes memory and incorrectly requires contiguous
        // mip order.
        for (std::size_t j = 0; j < count; ++j) {
            auto const mip = table + mips + (mip_start + j) * 32;
            auto const offset = body.integer(mip, 8);
            auto const length_bytes = body.integer(mip + 8, 8);
            auto const row_bytes = (format == 1 || format == 2) ? width * 4 :
                ((width + 3) / 4) * (format == 101 || format == 102 ? 8 : 16);
            auto const rows = (format == 1 || format == 2) ? height : (height + 3) / 4;
            require(format == 1 || format == 2 || (format >= 101 && format <= 105), "Unsupported NETD texture format");
            // Validate every byte range before any pointer or subtraction.
            require(offset >= start && offset - start <= size &&
                length_bytes <= size - (offset - start) && length_bytes == row_bytes * rows &&
                body.integer(mip + 16, 4) == width && body.integer(mip + 20, 4) == height &&
                body.integer(mip + 24, 4) == j && body.integer(mip + 28, 4) == 0,
                "Invalid NETD mip layout");
            auto const next_range=occupied_mip_ranges.lower_bound(offset);
            if(next_range!=occupied_mip_ranges.end() && next_range->first==offset) {
                require(next_range->second==length_bytes,
                        "Conflicting NETD mip alias size");
            } else {
                require(next_range==occupied_mip_ranges.end() ||
                        length_bytes<=next_range->first-offset,
                        "Overlapping NETD mip block ranges");
                if(next_range!=occupied_mip_ranges.begin()) {
                    auto const previous=std::prev(next_range);
                    require(previous->first+previous->second<=offset,
                            "Overlapping NETD mip block ranges");
                }
                occupied_mip_ranges.emplace_hint(next_range,offset,length_bytes);
            }
            first_mip=std::min(first_mip,offset);
            last_mip=std::max(last_mip,offset+length_bytes);
            // The runtime needs a compact texture and relative mip offsets;
            // it does not need the original shared/padded source span.
            auto const compact_offset=texture.data.size();
            require(compact_offset<=payload_size &&
                    length_bytes<=payload_size-compact_offset,
                    "NETD compact texture exceeds dictionary payload");
            auto const* encoded=body.at(payload+offset,length_bytes);
            texture.data.insert(texture.data.end(),encoded,encoded+length_bytes);
            texture.mips.push_back({static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height),compact_offset,
                static_cast<std::size_t>(length_bytes)});
            if (j + 1 < count)
                require(width > 1 || height > 1, "NETD mip chain exceeds texture dimensions");
            width = width > 1 ? width / 2 : 1;
            height = height > 1 ? height / 2 : 1;
        }
        // Outer entries may include older shared mip blocks, but must
        // precisely span all of their own referenced blocks.
        require(first_mip==start && last_mip==start+size,
                "NETD entry bounds do not match referenced mip blocks");
        if(legacy_bc3_mislabeled_bc7(texture))
            texture.format_id=103; // Correct effective GPU/software codec.
        expected_mip += count;
        result.push_back(std::move(texture));
    }
    require(expected_mip == mip_count, "Unclaimed NETD mip records");
    // Enforce complete coverage of the payload with unique, non-overlapping
    // encoded mip blocks and zero-only alignment gaps (<16 bytes). This
    // preserves corruption detection even when entries have shared spans.
    std::uint64_t covered=0;
    for(auto const& [start,length]:occupied_mip_ranges) {
        require(start>=covered, "Overlapping NETD mip block ranges");
        auto const gap=start-covered;
        require(gap<16, "Unexpected NETD mip payload gap");
        if(gap) {
            auto const* padding=body.at(payload+covered,gap);
            require(std::all_of(padding,padding+gap,
                [](std::uint8_t byte){return byte==0;}),
                "Nonzero NETD mip padding");
        }
        covered=start+length;
    }
    require(covered==payload_size, "NETD payload has unclaimed trailing bytes");
    return result;
}

std::vector<Texture> load_textures(AssetStore const& assets,
                                   std::string const& path) {
    return parse_texture_dictionary(detail::read_list_file(assets,path),path);
}
std::vector<Texture> load_textures_from_bytes(std::vector<std::uint8_t> const& bytes,
                                   std::string const& path) {
    return parse_texture_dictionary(detail::parse_list_file(bytes),path);
}

} // namespace northstarcpp::shared_assets
