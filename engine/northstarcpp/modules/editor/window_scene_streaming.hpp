#pragma once

#include <northstarcpp/assets.hpp>
#include <northstarcpp/scene_chunk_index.hpp>
#include <northstarcpp/scene_navigation_cells.hpp>
#include <northstarcpp/scene_world.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <variant>

namespace northstarcpp {
class PhysicsScene;

namespace window_detail {

bool same_asset_directory(std::filesystem::path const& a,std::filesystem::path const& b);

struct SceneStreamingPaths {
    std::filesystem::path const& scene;
    std::filesystem::path const& archive;
    std::filesystem::path const& archive_data;
    std::filesystem::path const& shared_assets;
};

struct WindowStreamingStats {
    std::size_t pending_chunks=0, decoded_chunks=0;
    std::size_t requests=0, resolved=0, failed=0, navigation_cells=0;
};

class WindowSceneStreaming {
public:
    void rebuild(Scene snapshot,SceneStreamingPaths const& paths,bool read_saved_chunks);
    void update(Scene const& authored,Vector3 position,std::string const& room,
                float seconds,PhysicsScene* physics);

    SceneWorld* world() const noexcept { return world_.get(); }
    AssetManager const* assets() const noexcept { return assets_.get(); }
    WindowStreamingStats stats() const;

private:
    void mount_assets(SceneStreamingPaths const& paths);
    void prefetch_assets();
    void clear_asset_requests();
    using Prefetch=std::variant<AssetRequest<std::vector<std::uint8_t>>,
        AssetRequest<shared_assets::DrawableResources>>;
    struct AssetRequestEntry {
        Prefetch request;
        bool completed=false;
    };
    std::unique_ptr<SceneWorld> world_;
    std::unique_ptr<SceneNavigationCells> navigation_;
    std::unique_ptr<AssetManager> assets_;
    // Destroy pending range reads and requests before their VFS.
    std::unique_ptr<SceneCellAsyncResidency> residency_;
    std::filesystem::path asset_source_;
    std::map<std::string,AssetRequestEntry,std::less<>> requests_;
    std::set<std::string,std::less<>> failed_;
    Vector3 previous_{0.f,0.f,0.f};
    bool previous_valid_=false;
};

} // namespace window_detail
} // namespace northstarcpp
