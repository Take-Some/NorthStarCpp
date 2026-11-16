// NorthStar Pack (.nsp) v1: bounded, random-access archive reader.
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace northstarcpp::vfs {
class NspArchive final {
public:
    struct Entry {
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        std::uint32_t crc32 = 0;
    };
    explicit NspArchive(std::filesystem::path const& path);
    Entry const* find(std::string_view name) const;
    void read(Entry const& entry, std::uint8_t* output) const;
    void read_range(Entry const& entry, std::uint64_t offset,
                    std::uint8_t* output, std::size_t length) const;
private:
    mutable std::mutex mutex;
    mutable std::ifstream input;
    std::map<std::string, Entry, std::less<>> files;
};
} // namespace northstarcpp::vfs
