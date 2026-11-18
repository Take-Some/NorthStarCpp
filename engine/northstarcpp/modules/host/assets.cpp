// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#include <northstarcpp/assets.hpp>
#include <northstarcpp/asset_roots.hpp>
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
namespace asset_paths {
namespace fs=std::filesystem;

fs::path executable_dir()
{
#ifdef _WIN32
    std::array<wchar_t,32768> name{};
    auto const length=GetModuleFileNameW(nullptr,name.data(),
        static_cast<DWORD>(name.size()));
    if(length && length<name.size())
        return fs::path(name.data()).parent_path();
#endif
    std::error_code ec;
    return fs::current_path(ec);
}

namespace {
bool is_engine_content_root(fs::path const& root)
{
    std::error_code ec;
    if(root.filename()!="sharedAssets" ||
       !fs::is_directory(root,ec) || ec ||
       fs::is_symlink(root,ec) || ec)return false;
    ec.clear();
    return fs::is_directory(root/"models",ec) ||
           fs::is_directory(root/"api",ec) ||
           fs::is_directory(root/"materials",ec);
}

fs::path from_anchor(fs::path const& start)
{
    if(start.empty())return {};
    std::error_code ec;
    auto parent=fs::absolute(start,ec);
    if(ec)return {};
    if(!fs::is_directory(parent,ec))parent=parent.parent_path();
    // Prefer the canonical engine source before legacy/installed layouts.
    // All consumers use this order, with no Qt, Player or prefab exceptions.
    constexpr std::array<char const*,5> layouts{{
        "engine/northstarcpp/resources/sharedAssets",
        "sharedAssets",
        "resources/sharedAssets",
        "share/northstarcpp/sharedAssets",
        "NorthStarCpp/engine/northstarcpp/resources/sharedAssets"
    }};
    for(unsigned depth=0;depth<16 && !parent.empty();++depth) {
        if(is_engine_content_root(parent)) {
            auto resolved=fs::weakly_canonical(parent,ec);
            if(!ec)return resolved;
        }
        for(auto const* relative:layouts) {
            auto const candidate=parent/fs::u8path(relative);
            if(!is_engine_content_root(candidate))continue;
            ec.clear();
            auto resolved=fs::weakly_canonical(candidate,ec);
            if(!ec)return resolved;
        }
        auto const next=parent.parent_path();
        if(next==parent)break;
        parent=next;
    }
    return {};
}
} // namespace

fs::path locate_shared_root(fs::path const& preferred,
                            fs::path const& fallback)
{
    auto first=from_anchor(preferred);
    return first.empty()?from_anchor(fallback):first;
}
} // namespace asset_paths

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
void AssetStore::mount_base_assets(std::filesystem::path const& anchor,
                                  std::filesystem::path const& shared_root)
{
    namespace fs=std::filesystem;
    mount_runtime_archives();
    auto const common=shared_root.empty()?
        asset_paths::locate_shared_root(anchor,asset_paths::executable_dir()):
        shared_root;
    if(common.empty())return;

    if(fs::is_regular_file(common) && common.extension()==".nsp") {
        // An explicit NSP must never be mounted twice under different
        // priorities when it already lives in the runtime archive folder.
        std::error_code ec;
        auto const discovered=runtime_archives_path()/common.filename();
        if(!fs::equivalent(common,discovered,ec) || ec)
            mount_archive("",common,common,-90);
        return;
    }
    if(!fs::is_directory(common))
        throw std::invalid_argument("Engine sharedAssets source is not a directory");

    std::error_code ec;
    if(fs::equivalent(common,anchor,ec) && !ec)return;
    ec.clear();
    if(fs::is_directory(anchor/"assets",ec)) {
        ec.clear();
        if(fs::equivalent(common,anchor/"assets",ec) && !ec)return;
    }
    mount_shared_assets(common);
}
void AssetStore::mount_project_assets(std::filesystem::path const& project_root,
                                      std::filesystem::path const& shared_root)
{
    namespace fs=std::filesystem;
    if(!fs::is_directory(project_root))
        throw std::invalid_argument("Project VFS root is not a directory");
    mount_base_assets(project_root,shared_root);
    mount_directory("",project_root,0); // scene metadata, legacy files
    auto const assets=project_root/"assets";
    if(fs::is_directory(assets))
        mount_directory("",assets,100); // canonical project asset override
}
void AssetStore::mount_content_assets(std::filesystem::path const& content_root,
                                     std::filesystem::path const& shared_root)
{
    if(!std::filesystem::is_directory(content_root))
        throw std::invalid_argument("Content VFS root is not a directory");
    mount_base_assets(content_root,shared_root);
    // The same project override tier is used by game and editor previews.
    mount_directory("",content_root,100);
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
