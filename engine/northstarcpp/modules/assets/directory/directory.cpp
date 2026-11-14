// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#include <northstarcpp/assets_plugin.h>
#include "module_provider.hpp"
#include "asset_paths.hpp"
#include "fat_archive.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace northstarcpp::vfs {

bool valid_component(std::string_view value)
{
    if (value.empty() || value.size() > max_path_bytes ||
        value.back() == '.' || value.back() == ' ' ||
        value.find_first_of(R"(/\:<>"|?*)") != std::string_view::npos ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32; }))
        return false;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0) != 0;
}

void validate_path(std::string_view path, bool allow_empty)
{
    if (path.empty() && allow_empty)
        return;
    if (path.size() > max_path_bytes)
        throw std::runtime_error("Virtual asset path is too long");
    for (;;) {
        auto const separator = path.find('/');
        if (!valid_component(path.substr(0, separator)))
            throw std::runtime_error("Invalid virtual asset path");
        if (separator == std::string_view::npos)
            return;
        path.remove_prefix(separator + 1);
    }
}

} // namespace northstarcpp::vfs

namespace {
namespace fs = std::filesystem;
using namespace northstarcpp::module_provider;
using namespace northstarcpp::vfs;

std::string virtual_path(char const* value, bool allow_empty)
{
    if (!value)
        throw std::runtime_error("Missing virtual asset path");
    validate_path(value, allow_empty);
    return value;
}

fs::path physical(char const* value)
{
    if (!value || !*value)
        throw std::runtime_error("Missing asset mount path");
    return fs::weakly_canonical(fs::absolute(fs::u8path(value)));
}

bool contained(fs::path const& parent, fs::path const& item)
{
    auto a = parent.begin();
    auto b = item.begin();
    for (; a != parent.end(); ++a, ++b)
        if (b == item.end() || *a != *b) return false;
    return true;
}

struct Mount {
    std::string prefix;
    fs::path path;
    fs::path data_path;
    std::shared_ptr<FatArchive> archive;
    std::int32_t priority;
};
struct Store {
    std::mutex mutex;
    std::vector<Mount> mounts; // Sorted by priority, prefix length, newest first.
};
struct Source {
    fs::path path;
    std::shared_ptr<FatArchive> archive;
    FatArchive::Entry const* entry = nullptr;

    std::uint64_t size() const
    {
        if (entry && entry->compressed)
            throw std::runtime_error("PPMd-compressed FAT/DB entries are not supported");
        auto const count = entry ? entry->size : fs::file_size(path);
        if (count > max_asset_bytes)
            throw std::runtime_error("Buffered asset size exceeds 256 MiB");
        return count;
    }
};

Store& store(void* ptr)
{
    if (!ptr) throw std::runtime_error("Missing asset context");
    return *static_cast<Store*>(ptr);
}

Source resolve(Store& state, char const* name)
{
    auto const key = virtual_path(name, false);
    std::lock_guard<std::mutex> lock(state.mutex);
    for (auto const& mount : state.mounts) {
        std::string_view relative = key;
        if (!mount.prefix.empty()) {
            auto const length = mount.prefix.size();
            if (key.size() <= length || key[length] != '/' || key.compare(0, length, mount.prefix) != 0)
                continue;
            relative.remove_prefix(length + 1);
        }

        if (mount.archive) {
            if (auto const* entry = mount.archive->find(relative))
                return {{}, mount.archive, entry};
            continue;
        }
        auto const candidate = fs::weakly_canonical(
            mount.path / fs::u8path(relative.begin(), relative.end()));
        if (!contained(mount.path, candidate))
            throw std::runtime_error("Mounted asset points outside its root");
        if (fs::is_regular_file(candidate))
            return {candidate, {}, nullptr};
    }
    throw std::runtime_error("Asset not found: " + key);
}

bool same_mount(Mount const& a, Mount const& b)
{
    return a.prefix == b.prefix && a.path == b.path && a.data_path == b.data_path;
}

void add_mount(Store& state, Mount mount)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (std::any_of(state.mounts.begin(), state.mounts.end(),
            [&](Mount const& existing) { return same_mount(existing, mount); }))
        throw std::runtime_error("Asset source already mounted");

    auto const position = std::find_if(state.mounts.begin(), state.mounts.end(),
        [&](Mount const& existing) {
            if (existing.priority != mount.priority)
                return existing.priority < mount.priority;
            // Prefixes that both match the same asset are nested.
            return existing.prefix.size() <= mount.prefix.size();
        });
    state.mounts.insert(position, std::move(mount));
}

void remove_mount(Store& state, Mount const& mount)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    auto const position = std::find_if(state.mounts.begin(), state.mounts.end(),
        [&](Mount const& existing) { return same_mount(existing, mount); });
    if (position == state.mounts.end())
        throw std::runtime_error("Asset mount not found");
    state.mounts.erase(position);
}

void* create()
{
    Store* ptr = nullptr;
    guarded([&] { ptr = new Store; });
    return ptr;
}
void destroy(void* ptr) { delete static_cast<Store*>(ptr); }

int mount_directory(void* ptr, char const* prefix, char const* folder, std::int32_t priority)
{
    return guarded([&] {
        auto key = virtual_path(prefix, true);
        auto path = physical(folder);
        if (!fs::is_directory(path))
            throw std::runtime_error("Asset mount directory does not exist");
        add_mount(store(ptr), {std::move(key), std::move(path), {}, {}, priority});
    });
}
int unmount_directory(void* ptr, char const* prefix, char const* folder)
{
    return guarded([&] {
        remove_mount(store(ptr), {virtual_path(prefix, true), physical(folder), {}, {}, 0});
    });
}

int mount_archive(void* ptr, char const* prefix, char const* index, char const* data, std::int32_t priority)
{
    return guarded([&] {
        auto key = virtual_path(prefix, true);
        auto index_path = physical(index);
        auto data_path = physical(data);
        if (!fs::is_regular_file(index_path) || !fs::is_regular_file(data_path))
            throw std::runtime_error("FAT/DB archive files do not exist");
        auto archive = std::make_shared<FatArchive>(index_path, data_path);
        add_mount(store(ptr), {std::move(key), std::move(index_path), std::move(data_path),
            std::move(archive), priority});
    });
}
int unmount_archive(void* ptr, char const* prefix, char const* index, char const* data)
{
    return guarded([&] {
        remove_mount(store(ptr), {virtual_path(prefix, true), physical(index), physical(data), {}, 0});
    });
}

int file_size(void* ptr, char const* name, std::uint64_t* size)
{
    return guarded([&] {
        if (!size) throw std::runtime_error("Missing size output buffer");
        *size = resolve(store(ptr), name).size();
    });
}
int read(void* ptr, char const* name, std::uint8_t* bytes, std::uint64_t capacity, std::uint64_t* received)
{
    return guarded([&] {
        if (!received) throw std::runtime_error("Missing asset length pointer");
        auto const source = resolve(store(ptr), name);
        auto const size = source.size();
        if (capacity < size || (size && !bytes))
            throw std::runtime_error("Asset buffer capacity is insufficient");
        if (source.archive) {
            source.archive->read(*source.entry, bytes);
        } else {
            std::ifstream input(source.path, std::ios::binary);
            if (!input || (size && !input.read(reinterpret_cast<char*>(bytes), static_cast<std::streamsize>(size))))
                throw std::runtime_error("Cannot read mounted asset");
        }
        *received = size;
    });
}

int read_range(void* ptr, char const* name, std::uint64_t offset,
               std::uint8_t* bytes, std::uint64_t length, std::uint64_t* received)
{
    return guarded([&] {
        if (!received || (length && !bytes))
            throw std::runtime_error("Invalid asset range output buffer");
        auto const source=resolve(store(ptr),name);
        auto const total=source.size();
        if (offset>total || length>total-offset)
            throw std::runtime_error("Asset read range exceeds logical file");
        if(source.archive) {
            source.archive->read_range(*source.entry,offset,bytes,
                                       static_cast<std::size_t>(length));
        } else if(length) {
            std::ifstream input(source.path,std::ios::binary);
            if (!input || !input.seekg(static_cast<std::streamoff>(offset)) ||
                !input.read(reinterpret_cast<char*>(bytes),
                            static_cast<std::streamsize>(length)))
                throw std::runtime_error("Cannot read mounted asset range");
        }
        *received=length;
    });
}
NorthStarAssetsApi const api{create, destroy, mount_directory, unmount_directory, file_size, read};
NorthStarAssetsRangeApi const range_api{read_range};
NorthStarAssetsArchiveApi const archive_api{mount_archive, unmount_archive};
NorthStarModule const module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_ASSETS, "Directory VFS", "1.2.0",
    NORTHSTARCPP_ASSETS_ABI, sizeof(api), &api, last_error};
NorthStarModule const archive_module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_ASSETS, "Directory VFS", "1.2.0",
    NORTHSTARCPP_ASSETS_ARCHIVE_ABI, sizeof(archive_api), &archive_api, last_error};
NorthStarModule const range_module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_ASSETS, "Directory VFS", "1.2.0",
    NORTHSTARCPP_ASSETS_RANGE_ABI, sizeof(range_api), &range_api, last_error};
}

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarModule*
northstar_module_query(uint32_t host, char const* capability, uint32_t version)
{
    auto const& table = version == NORTHSTARCPP_ASSETS_RANGE_ABI ? range_module :
        (version == NORTHSTARCPP_ASSETS_ARCHIVE_ABI ? archive_module : module);
    return northstarcpp::module_provider::match(table, host, capability, version);
}
