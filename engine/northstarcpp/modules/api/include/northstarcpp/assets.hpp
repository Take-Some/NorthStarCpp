// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#pragma once
#include <northstarcpp/assets_plugin.h>
#include <northstarcpp/asset_streaming.hpp>
#include <northstarcpp/fps_assets.hpp>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace northstarcpp {
// The host owns the plugin context and never depends on any provider's C++ types.
// Use asset paths relative to a mounted namespace. A higher-priority mount shadows
// an existing file with the same virtual path.
class AssetStore {
public:
    AssetStore();
    ~AssetStore();
    AssetStore(AssetStore const&) = delete;
    AssetStore& operator=(AssetStore const&) = delete;

    std::string provider_name() const;
    void mount_directory(std::string const& prefix, std::filesystem::path const& root,
                         int32_t priority = 0);
    void unmount_directory(std::string const& prefix, std::filesystem::path const& root);
    // For a combined FAT/DB file, pass its path as both index and data.
    void mount_archive(std::string const& prefix, std::filesystem::path const& index,
                       std::filesystem::path const& data, int32_t priority = 0);
    void unmount_archive(std::string const& prefix, std::filesystem::path const& index,
                         std::filesystem::path const& data);
    // Shared content is a base VFS layer. Project mounts can override it.
    void mount_shared_assets(std::filesystem::path const& root);
    shared_assets::Material load_material(std::string const& reference) const;
    shared_assets::Drawable load_drawable(std::string const& reference) const;
    shared_assets::Texture load_texture(std::string const& reference) const;
    shared_assets::DrawableResources load_drawable_resources(std::string const& reference) const;
    // Requests enqueue work. Poll ready() before get() in a frame loop.
    AssetRequest<std::vector<std::uint8_t>> request_bytes(std::string path,
        AssetPriority priority = AssetPriority::normal) const;
    AssetRequest<Scene> request_scene(std::string path,
        AssetPriority priority = AssetPriority::critical) const;
    AssetRequest<FpsTuning> request_fps_tuning(std::string path,
        AssetPriority priority = AssetPriority::critical) const;
    AssetRequest<FpsAudioClip> request_audio_clip(std::string path,
        AssetPriority priority = AssetPriority::normal) const;
    AssetRequest<shared_assets::Material> request_material(std::string reference,
        AssetPriority priority = AssetPriority::normal) const;
    AssetRequest<shared_assets::Drawable> request_drawable(std::string reference,
        AssetPriority priority = AssetPriority::normal) const;
    AssetRequest<shared_assets::Texture> request_texture(std::string reference,
        AssetPriority priority = AssetPriority::normal) const;
    AssetRequest<shared_assets::DrawableResources> request_drawable_resources(std::string reference,
        AssetPriority priority = AssetPriority::normal) const;
    void set_streaming_budget(std::size_t bytes);
    AssetStreamingStats streaming_stats() const;

    std::uint64_t size(std::string const& asset_path) const;
    std::vector<std::uint8_t> read(std::string const& asset_path) const;
    // Positioned VFS read (directories and FAT/DB mounts). With an older
    // provider, falls back to a bounded full-file read for compatibility.
    std::vector<std::uint8_t> read_range(std::string const& asset_path,
                                          std::uint64_t offset,
                                          std::size_t length) const;
    bool native_range_reads() const;

private:
    template<class T, class Loader>
    AssetRequest<T> request(int kind, std::string path, AssetPriority priority, Loader loader) const;
    struct State;
    std::unique_ptr<State> state;
};
using AssetManager = AssetStore;
} // namespace northstarcpp
