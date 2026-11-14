// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
// PC archive layout ported from Kayla Verner's FAT/DB code in compat.
// Copyright (C) Kayla's Systems - 2010
#include "fat_archive.hpp"
#include "asset_paths.hpp"

#include <boost/crc.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace northstarcpp::vfs {
namespace {

constexpr std::uint32_t header_size = 24;
constexpr std::uint32_t max_index_bytes = 64u * 1024 * 1024;
constexpr std::uint32_t max_nodes = 1'000'000;
constexpr std::uint64_t empty_folder = 0xffffffff;
constexpr std::uint16_t db_flag = 0x2000;
constexpr std::uint16_t folder_flag = 0x8000;
constexpr std::uint16_t inline_flag = 0x0400;
constexpr std::uint16_t compressed_flag = 0x0800;
constexpr std::uint16_t known_flags = db_flag | folder_flag | inline_flag | compressed_flag;

using Checksum = boost::crc_optimal<32, 0x04C11DB7, 0, 0, true, false>;

[[noreturn]] void invalid_archive(char const* detail)
{
    throw std::runtime_error(std::string("Invalid PC FAT/DB archive: ") + detail);
}

std::uint64_t integer(std::uint8_t const* bytes, std::size_t length)
{
    std::uint64_t value = 0;
    for (std::size_t i = length; i != 0; --i)
        value = (value << 8) | bytes[i - 1];
    return value;
}

bool fits(std::uint64_t total, std::uint64_t offset, std::uint64_t size)
{
    return offset <= total && size <= total - offset;
}

std::uint64_t length(std::ifstream& file)
{
    file.seekg(0, std::ios::end);
    auto const end = file.tellg();
    if (end < 0)
        throw std::runtime_error("Cannot measure FAT/DB file");
    return static_cast<std::uint64_t>(end);
}

void read_at(std::ifstream& file, std::uint64_t offset, std::uint8_t* bytes, std::size_t size)
{
    if (size == 0)
        return;
    file.clear();
    if (!file.seekg(static_cast<std::streamoff>(offset)) ||
        !file.read(reinterpret_cast<char*>(bytes), static_cast<std::streamsize>(size)))
        throw std::runtime_error("Cannot read FAT/DB file");
}

struct Index {
    std::vector<std::uint8_t> bytes;
    std::uint64_t data_length;
    std::uint64_t payload_start;
    std::uint64_t remaining;
    std::uint64_t path_budget;
    std::map<std::string, FatArchive::Entry, std::less<>>& files;

    std::uint64_t field(std::uint64_t offset, std::size_t width) const
    {
        if (!fits(bytes.size(), offset, width))
            invalid_archive("truncated node");
        return integer(bytes.data() + offset, width);
    }

    std::uint64_t visit(std::uint64_t node, std::uint64_t parent, std::uint64_t previous,
                        std::string& path, unsigned depth)
    {
        // PC pointer slots are 64-bit file offsets relative to the FAT body.
        // Sibling and parent offsets point at the fat_node base inside each record.
        if (depth > 128 || remaining == 0 || node % 8 != 0 || !fits(bytes.size(), node, 35))
            invalid_archive("invalid node count, depth or offset");
        --remaining;
        auto const next = field(node + 8, 8);
        if ((next && next <= node) || field(node + 16, 8) != previous || field(node + 24, 8) != parent)
            invalid_archive("broken node links");
        auto const flags = field(node + 32, 2);
        if (!(flags & db_flag) || (flags & ~std::uint64_t(known_flags)))
            invalid_archive("unsupported node flags");
        auto const folder = (flags & folder_flag) != 0;
        auto const inlined = (flags & inline_flag) != 0;
        auto const compressed = (flags & compressed_flag) != 0;
        if ((folder && (inlined || compressed)) || (depth == 0 && (!folder || next)))
            invalid_archive("invalid folder");

        auto const name_start = node + 34;
        auto const scan_length = std::min<std::uint64_t>(bytes.size() - name_start, max_path_bytes + 1);
        auto const* name_bytes = reinterpret_cast<char const*>(bytes.data() + name_start);
        auto const* name_end = static_cast<char const*>(std::memchr(name_bytes, 0, scan_length));
        if (!name_end)
            invalid_archive("unterminated node name");
        std::string_view const name(name_bytes, name_end - name_bytes);
        auto const path_size = path.size();
        if (depth != 0) {
            if (!valid_component(name) || path_size + (path.empty() ? 0 : 1) + name.size() > max_path_bytes)
                invalid_archive("invalid virtual file name");
            if (!path.empty())
                path.push_back('/');
            path.append(name);
        }

        if (folder) {
            if (node < 8)
                invalid_archive("truncated folder");
            std::uint64_t previous_child = 0;
            auto child = field(node - 8, 8);
            // The legacy writer stores UINT32_MAX for an empty folder.
            while (child != 0 && child != empty_folder) {
                if (child <= node)
                    invalid_archive("invalid child offset");
                auto const sibling = visit(child, node, previous_child, path, depth + 1);
                previous_child = child;
                child = sibling;
            }
        } else {
            // fat_db_file_node_base is 24 bytes. Inline metadata adds 16 bytes
            // and compressed records add an 8-byte slot for uncompressed size.
            std::uint64_t const base_size = 24 + (inlined ? 16 : 0) + (compressed ? 8 : 0);
            if (node < base_size)
                invalid_archive("truncated file record");
            auto const base = node - base_size;
            auto const stored_size = field(base + 16, 4);
            auto const raw_size = compressed ? field(node - 8, 4) : stored_size;
            auto const checksum = field(base + 20, 4);
            auto offset = field(base + (inlined ? 24 : 8), 8);
            if (inlined) {
                if (field(base + 32, 4) != stored_size || offset < name_start + name.size() + 1 ||
                    !fits(bytes.size(), offset, stored_size))
                    invalid_archive("inline payload outside FAT");
                offset += header_size;
            } else if (offset < payload_start || !fits(data_length, offset, stored_size)) {
                invalid_archive("file payload outside DB");
            }
            if (path.size() > path_budget)
                invalid_archive("expanded asset paths exceed index limit");
            path_budget -= path.size();
            auto const inserted = files.emplace(path, FatArchive::Entry{offset,
                static_cast<std::uint32_t>(raw_size), static_cast<std::uint32_t>(checksum), inlined, compressed});
            if (!inserted.second)
                invalid_archive("duplicate virtual file");
        }
        path.resize(path_size);
        return next;
    }
};

} // namespace

FatArchive::FatArchive(std::filesystem::path const& index, std::filesystem::path const& data)
    : index_file(index, std::ios::binary)
{
    if (!index_file)
        throw std::runtime_error("Cannot open FAT index");
    bool const combined = index == data || std::filesystem::equivalent(index, data);
    if (!combined) {
        data_file.open(data, std::ios::binary);
        if (!data_file)
            throw std::runtime_error("Cannot open DB payload");
    }
    auto const index_length = length(index_file);
    if (index_length < header_size)
        invalid_archive("truncated header");
    std::array<std::uint8_t, header_size> header{};
    read_at(index_file, 0, header.data(), header.size());
    if (std::memcmp(header.data(), "little-endian", 14) != 0)
        invalid_archive("expected a little-endian PC archive");
    auto const count = integer(header.data() + 16, 4);
    auto const size = integer(header.data() + 20, 4);
    if (count == 0 || count > max_nodes || size > max_index_bytes ||
        !fits(index_length, header_size, size) || count > size / 35)
        invalid_archive("invalid FAT size or node count");

    Index table{std::vector<std::uint8_t>(static_cast<std::size_t>(size)),
        combined ? index_length : length(data_file), combined ? header_size + size : 0,
        count, max_index_bytes, files};
    read_at(index_file, header_size, table.bytes.data(), table.bytes.size());
    std::string path;
    table.visit(8, 0, 0, path, 0);
    if (table.remaining != 0)
        invalid_archive("node count mismatch");
}

FatArchive::Entry const* FatArchive::find(std::string_view path) const
{
    auto const found = files.find(path);
    return found == files.end() ? nullptr : &found->second;
}

void FatArchive::read_range(Entry const& entry, std::uint64_t offset,
                            std::uint8_t* bytes, std::size_t count) const
{
    if (entry.compressed)
        throw std::runtime_error("PPMd-compressed FAT/DB entries are not supported");
    if (offset > entry.size || count > entry.size - offset ||
        (count && !bytes))
        throw std::runtime_error("FAT/DB range extends beyond asset");
    std::lock_guard<std::mutex> lock(mutex);
    auto& input = entry.inlined || !data_file.is_open() ? index_file : data_file;
    read_at(input, entry.offset + offset, bytes, count);
}

void FatArchive::read(Entry const& entry, std::uint8_t* bytes) const
{
    if (entry.compressed)
        throw std::runtime_error("PPMd-compressed FAT/DB entries are not supported");
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto& input = entry.inlined || !data_file.is_open() ? index_file : data_file;
        read_at(input, entry.offset, bytes, entry.size);
    }
    // Older fs_pack deduplication can leave the checksum unset (zero).
    if (entry.checksum != 0) {
        // This vendored Boost version lazily initializes its CRC table.
        static Checksum const initialized_checksum;
        auto checksum = initialized_checksum;
        if (entry.size != 0)
            checksum.process_bytes(bytes, entry.size);
        if (checksum.checksum() != entry.checksum)
            throw std::runtime_error("FAT/DB asset checksum mismatch");
    }
}

} // namespace northstarcpp::vfs
