// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace northstarcpp::vfs {

class FatArchive {
public:
    struct Entry {
        std::uint64_t offset;
        std::uint32_t size;
        std::uint32_t checksum;
        bool inlined;
        bool compressed;
    };

    FatArchive(std::filesystem::path const& index, std::filesystem::path const& data);
    Entry const* find(std::string_view path) const;
    void read(Entry const& entry, std::uint8_t* bytes) const;
    // The caller verifies the enclosing NEF8 chunk hash/CRC. Archive-level
    // whole-file CRC cannot be checked for a partial range.
    void read_range(Entry const& entry, std::uint64_t offset,
                    std::uint8_t* bytes, std::size_t length) const;

private:
    std::map<std::string, Entry, std::less<>> files;
    mutable std::mutex mutex;
    mutable std::ifstream index_file;
    mutable std::ifstream data_file;
};

} // namespace northstarcpp::vfs
