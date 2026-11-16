// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#include <northstarcpp/assets.hpp>
#include "module_loader.hpp"
#include "asset_streaming.hpp"
#include "asset_streaming_sizes.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace northstarcpp {
namespace {

std::filesystem::path runtime_archives_path()
{
#ifdef _WIN32
    // Resolve ./archive beside the exe, including Qt tools in bin/qt_tools.
    std::array<wchar_t,32768> buffer{};
    auto const length=GetModuleFileNameW(nullptr,buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if(length && length<buffer.size()) {
        auto const executable=std::filesystem::path(buffer.data()).parent_path();
        auto const local=executable/"archive";
        if(std::filesystem::is_directory(local))return local;
        if(executable.filename()=="qt_tools") {
            auto const shared=executable.parent_path()/"archive";
            if(std::filesystem::is_directory(shared))return shared;
        }
    }
#endif
    return {};
}
std::string folded_filename(std::filesystem::path const& path)
{
    auto name=path.filename().u8string();
    std::transform(name.begin(),name.end(),name.begin(),
        [](unsigned char c) {
            return static_cast<char>(c>='A' && c<='Z'?c-'A'+'a':c);
        });
    return name;
}

char const* key(std::string const& value)
{
    if (value.find('\0') != std::string::npos)
        throw std::invalid_argument("Unexpected NUL in virtual asset path");
    return value.c_str();
}

} // namespace
struct AssetStore::State {
    Module module{NORTHSTARCPP_CAPABILITY_ASSETS};
    NorthStarAssetsApi const& api = *module.query<NorthStarAssetsApi>(NORTHSTARCPP_ASSETS_ABI);
    NorthStarAssetsArchiveApi const* archive = module.query_extension<NorthStarAssetsArchiveApi>(NORTHSTARCPP_ASSETS_ARCHIVE_ABI);
    NorthStarAssetsRangeApi const* range = module.query_extension<NorthStarAssetsRangeApi>(NORTHSTARCPP_ASSETS_RANGE_ABI);
    std::mutex mutex;
    detail::AssetStreaming streaming;
    void* context = nullptr;
    State()
    {
        context = api.create();
        if (!context)
            throw module.error("Cannot create asset provider");
    }
    ~State() { api.destroy(context); }
    NorthStarAssetsArchiveApi const& archive_api() const
    {
        if (!archive)
            throw module.error("Asset provider does not support archive mounts");
        return *archive;
    }
    std::uint64_t size(char const* path) const
    {
        std::uint64_t length = 0;
        if (!api.file_size(context, path, &length))
            throw module.error("Cannot locate asset");
        return length;
    }
};
AssetStore::AssetStore() : state(std::make_unique<State>()) {}
AssetStore::~AssetStore() { state->streaming.stop(); }
std::string AssetStore::provider_name() const { return state->module.provider(); }
void AssetStore::mount_project_assets(std::filesystem::path const& project_root,
                                      std::filesystem::path const& shared_root)
{
    namespace fs = std::filesystem;
    if (!fs::is_directory(project_root))
        throw std::invalid_argument("Project VFS root is not a directory");
    auto const assets = project_root / "assets";
    auto const has_assets = fs::is_directory(assets);
    mount_runtime_archives();
    if(!shared_root.empty()) {
        if(fs::is_regular_file(shared_root) && shared_root.extension()==".nsp") {
            // Compatibility for explicit pack mounts; avoid a second mount.
            auto const discovered=runtime_archives_path()/shared_root.filename();
            std::error_code ec;
            bool const duplicate=fs::equivalent(shared_root,discovered,ec) && !ec;
            if(!duplicate)mount_archive("",shared_root,shared_root,-90);
        } else {
            if(!fs::is_directory(shared_root))
                throw std::invalid_argument("Engine sharedAssets source is not a directory");
            std::error_code ec;
            bool const is_project=fs::equivalent(project_root,shared_root,ec) && !ec;
            ec.clear();
            bool const is_assets=has_assets && fs::equivalent(assets,shared_root,ec) && !ec;
            if(!is_project && !is_assets)
                mount_directory("",shared_root,-80); // editable development overlay
        }
    }
    mount_directory("", project_root, 0); // scripts/, project metadata, scenes
    if (has_assets)
        mount_directory("", assets, 100); // textures/, models/, gameplay/
}
void AssetStore::mount_runtime_archives(std::filesystem::path const& root)
{
    namespace fs=std::filesystem;
    auto const archives=root.empty()?runtime_archives_path():fs::absolute(root);
    if(archives.empty())return; // Isolated tests/projects may have no runtime packs.
    if(!fs::is_directory(archives))
        throw std::invalid_argument("NSP archive directory does not exist: "+
            archives.u8string());
    std::vector<fs::path> packs;
    for(auto const& entry:fs::directory_iterator(archives)) {
        if(entry.is_symlink() || !entry.is_regular_file())continue;
        auto const filename=folded_filename(entry.path());
        if(filename.size()>4 && filename.substr(filename.size()-4)==".nsp")
            packs.push_back(entry.path());
    }
    if(packs.size()>4096)
        throw std::runtime_error("NSP archive directory has more than 4096 packs");
    std::sort(packs.begin(),packs.end(),[](fs::path const& a,fs::path const& b) {
        return folded_filename(a)<folded_filename(b);
    });
    // Baseline < addon packs < project root < project/assets.
    // Within the addon tier, later alphabetical names override earlier ones.
    for(auto const& pack:packs) {
        int const priority=folded_filename(pack)=="northstar_shared.nsp"?-100:-50;
        mount_archive("",pack,pack,priority);
        std::clog<<"[engine.assets] mounted NSP "<<pack.u8string()
                 <<" priority="<<priority<<"\n";
    }
}
void AssetStore::mount_directory(std::string const& prefix,
    std::filesystem::path const& root, int32_t priority)
{
    auto const utf8 = std::filesystem::absolute(root).u8string();
    std::unique_lock<std::shared_mutex> mounted(state->streaming.mounts);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->api.mount_directory(state->context, key(prefix), utf8.c_str(), priority))
        throw state->module.error("Cannot mount asset directory");
    state->streaming.invalidate();
}
void AssetStore::unmount_directory(std::string const& prefix,
    std::filesystem::path const& root)
{
    auto const utf8 = std::filesystem::absolute(root).u8string();
    std::unique_lock<std::shared_mutex> mounted(state->streaming.mounts);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->api.unmount_directory(state->context, key(prefix), utf8.c_str()))
        throw state->module.error("Cannot unmount asset directory");
    state->streaming.invalidate();
}
void AssetStore::mount_archive(std::string const& prefix, std::filesystem::path const& index,
    std::filesystem::path const& data, int32_t priority)
{
    auto const index_path = std::filesystem::absolute(index).u8string();
    auto const data_path = std::filesystem::absolute(data).u8string();
    std::unique_lock<std::shared_mutex> mounted(state->streaming.mounts);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->archive_api().mount_archive(state->context, key(prefix),
            index_path.c_str(), data_path.c_str(), priority))
        throw state->module.error("Cannot mount asset archive");
    state->streaming.invalidate();
}
void AssetStore::unmount_archive(std::string const& prefix, std::filesystem::path const& index,
    std::filesystem::path const& data)
{
    auto const index_path = std::filesystem::absolute(index).u8string();
    auto const data_path = std::filesystem::absolute(data).u8string();
    std::unique_lock<std::shared_mutex> mounted(state->streaming.mounts);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->archive_api().unmount_archive(state->context, key(prefix),
            index_path.c_str(), data_path.c_str()))
        throw state->module.error("Cannot unmount asset archive");
    state->streaming.invalidate();
}
std::uint64_t AssetStore::size(std::string const& asset_path) const
{
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->size(key(asset_path));
}
bool AssetStore::native_range_reads() const { return state->range != nullptr; }

std::vector<std::uint8_t> AssetStore::read_range(
    std::string const& asset_path, std::uint64_t offset,
    std::size_t length) const
{
    if(length > 16u*1024u*1024u)
        throw std::runtime_error("Asset range exceeds 16 MiB");
    if(!state->range) {
        // ABI 1 compatibility, not zero-copy or bounded I/O.
        auto full=read(asset_path);
        if(offset>full.size() || length>full.size()-offset)
            throw std::runtime_error("Asset read range exceeds file");
        auto first=full.begin()+static_cast<std::size_t>(offset);
        return {first,first+length};
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    auto const path=key(asset_path);
    auto const size=state->size(path);
    if(offset>size || length>size-offset)
        throw std::runtime_error("Asset range exceeds file");
    std::vector<std::uint8_t> data(length);
    std::uint64_t received=0;
    if(!state->range->read_range(state->context,path,offset,data.data(),
                                length,&received) || received!=length)
        throw state->module.error("Cannot read asset range");
    return data;
}
std::vector<std::uint8_t> AssetStore::read(std::string const& asset_path) const
{
    std::lock_guard<std::mutex> lock(state->mutex);
    auto const* path = key(asset_path);
    auto const size = state->size(path);
    if (size > (256ull << 20) || size > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("Asset too large for buffered access");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    uint64_t received = 0;
    if (!state->api.read(state->context, path, data.data(), size, &received) ||
        received != size)
        throw state->module.error("Cannot read asset");
    return data;
}

namespace {
enum StreamKind { bytes, scene, fps_tuning, audio_clip, material, drawable, texture, drawable_resources };
}

template<class T, class Loader>
AssetRequest<T> AssetStore::request(int kind, std::string path, AssetPriority priority, Loader loader) const
{
    auto record = state->streaming.request(kind, path, priority,
        [this, path, loader] {
            auto value = std::make_shared<T const>(loader(*this, path));
            return detail::StreamValue{value, sizeof(T) + detail::memory_size(*value)};
        });
    if (!record)
        return {};
    auto result = record->result;
    auto generation = record->generation;
    return AssetRequest<T>(std::move(result), std::move(record), std::move(generation));
}

AssetRequest<std::vector<std::uint8_t>> AssetStore::request_bytes(std::string path, AssetPriority priority) const
{
    return request<std::vector<std::uint8_t>>(bytes, std::move(path), priority,
        [](AssetStore const& assets, std::string const& key) { return assets.read(key); });
}

AssetRequest<Scene> AssetStore::request_scene(std::string path, AssetPriority priority) const
{
    return request<Scene>(scene, std::move(path), priority,
        [](AssetStore const& assets, std::string const& key) {
            auto const source = assets.read(key);
            return parse_scene_data(std::string(source.begin(), source.end()));
        });
}

AssetRequest<FpsTuning> AssetStore::request_fps_tuning(std::string path, AssetPriority priority) const
{
    return request<FpsTuning>(fps_tuning, std::move(path), priority,
        [](AssetStore const& assets, std::string const& key) {
            auto const source = assets.read(key);
            return parse_fps_tuning(std::string(source.begin(), source.end()));
        });
}

AssetRequest<FpsAudioClip> AssetStore::request_audio_clip(std::string path, AssetPriority priority) const
{
    return request<FpsAudioClip>(audio_clip, std::move(path), priority,
        [](AssetStore const& assets, std::string const& key) { return decode_fps_wav(assets.read(key)); });
}

AssetRequest<shared_assets::Material> AssetStore::request_material(std::string path, AssetPriority priority) const
{
    return request<shared_assets::Material>(material, std::move(path), priority,
        [](AssetStore const& assets, std::string const& key) { return assets.load_material(key); });
}

AssetRequest<shared_assets::Drawable> AssetStore::request_drawable(std::string path, AssetPriority priority) const
{
    return request<shared_assets::Drawable>(drawable, std::move(path), priority,
        [](AssetStore const& assets, std::string const& key) { return assets.load_drawable(key); });
}

AssetRequest<shared_assets::Texture> AssetStore::request_texture(std::string path, AssetPriority priority) const
{
    return request<shared_assets::Texture>(texture, std::move(path), priority,
        [](AssetStore const& assets, std::string const& key) { return assets.load_texture(key); });
}

AssetRequest<shared_assets::DrawableResources> AssetStore::request_drawable_resources(
    std::string path, AssetPriority priority) const
{
    return request<shared_assets::DrawableResources>(drawable_resources, std::move(path), priority,
        [](AssetStore const& assets, std::string const& key) { return assets.load_drawable_resources(key); });
}

void AssetStore::set_streaming_budget(std::size_t bytes) { state->streaming.set_budget(bytes); }
AssetStreamingStats AssetStore::streaming_stats() const { return state->streaming.stats(); }

} // namespace northstarcpp
