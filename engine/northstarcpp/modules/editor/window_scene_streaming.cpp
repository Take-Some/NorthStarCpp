#include "window_scene_streaming.hpp"
#include <northstarcpp/asset_roots.hpp>
#include <northstarcpp/physics.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string_view>

namespace northstarcpp::window_detail {
namespace fs=std::filesystem;
namespace {
constexpr std::size_t prefetch_limit=32;

template<class Byte>
bool is_chunked_scene(Byte const* signature)
{
    return signature[0]=='N' && signature[1]=='E' && signature[2]=='F' &&
        signature[3]=='8' && signature[10]==2 && signature[12]==2;
}

void warn_prefetch(std::string_view path,std::exception const& error)
{
    std::fprintf(stderr,"[WARN] [engine.assets] Skipping streaming asset '%.*s': %s\n",
        static_cast<int>(path.size()),path.data(),error.what());
}
}

bool same_asset_directory(fs::path const& a,fs::path const& b)
{
    if(a.empty() || b.empty())return false;
    std::error_code error;
    const bool same=fs::equivalent(a,b,error);
    return !error && same;
}

void WindowSceneStreaming::clear_asset_requests()
{
    requests_.clear();
    failed_.clear();
    assets_.reset();
    asset_source_.clear();
}

void WindowSceneStreaming::rebuild(Scene snapshot,SceneStreamingPaths const& paths,
                                   bool read_saved_chunks)
{
    residency_.reset();
    previous_valid_=false;
    if(snapshot.version!=3) {
        navigation_.reset();
        world_.reset();
        clear_asset_requests();
        return;
    }

    world_=std::make_unique<SceneWorld>(std::move(snapshot));
    navigation_=std::make_unique<SceneNavigationCells>(world_->source());
    mount_assets(paths);
    if(read_saved_chunks && !paths.scene.empty()) {
        try {
            if(!paths.archive.empty() && assets_) {
                const auto path=paths.scene.generic_u8string();
                const auto signature=assets_->read_range(path,0,16);
                if(signature.size()==16 && is_chunked_scene(signature.data()))
                    residency_=std::make_unique<SceneCellAsyncResidency>(*assets_,path);
            } else if(fs::is_regular_file(paths.scene)) {
                std::ifstream source(paths.scene,std::ios::binary);
                std::array<char,16> signature{};
                source.read(signature.data(),signature.size());
                if(source.gcount()==16 && is_chunked_scene(signature.data()))
                    residency_=std::make_unique<SceneCellAsyncResidency>(paths.scene);
            }
        } catch(std::exception const& error) {
            OutputDebugStringA(error.what());
        }
    }
    if(residency_)world_->enable_payload_gate(true);
}

void WindowSceneStreaming::mount_assets(SceneStreamingPaths const& paths)
{
    if(paths.scene.empty())return;
    const auto source=fs::absolute(paths.scene).lexically_normal();
    if(assets_ && asset_source_==source)return;

    clear_asset_requests();
    auto store=std::make_unique<AssetManager>();
    bool mounted=false;
    if(!paths.archive.empty()) {
        store->mount_runtime_archives();
        auto const shared=paths.shared_assets.empty()?
            asset_paths::locate_shared_root(source,asset_paths::executable_dir()):
            paths.shared_assets;
        if(!shared.empty())store->mount_shared_assets(shared);
        store->mount_archive("",paths.archive,
            paths.archive_data.empty()?paths.archive:paths.archive_data);
        mounted=true;
    } else {
        store->mount_project_assets(source.parent_path(),paths.shared_assets);
        mounted=true;
    }
    if(mounted) {
        store->set_streaming_budget(256ull*1024*1024);
        asset_source_=source;
        assets_=std::move(store);
    }
}

void WindowSceneStreaming::prefetch_assets()
{
    if(!assets_)return;
    // Walk exactly the prefix the scheduler consumes. The stable source
    // snapshot owns the strings for this call.
    std::array<std::string_view,prefetch_limit> selected;
    auto last=selected.begin();
    const auto add=[&](auto const& indices) {
        for(auto index:indices) {
            if(last==selected.end())break;
            const auto& asset=world_->source().objects[index].node.asset;
            if(!asset.empty() && std::find(selected.begin(),last,asset)==last)
                *last++=asset;
        }
    };
    add(world_->visible_indices());
    add(world_->resident_indices());
    for(auto it=requests_.begin();it!=requests_.end();) {
        if(std::find(selected.begin(),last,it->first)==last)it=requests_.erase(it);
        else ++it;
    }
    for(auto it=selected.begin();it!=last;++it) {
        const auto path=*it;
        if(failed_.find(path)!=failed_.end() || requests_.find(path)!=requests_.end())continue;
        const auto priority=requests_.size()<8?AssetPriority::critical:AssetPriority::normal;
        try {
            const std::string source(path);
            // Standalone YDD/YDR/OBJ references are files. Drawable resource
            // requests are for dictionaries whose entry is explicitly selected.
            Prefetch request=path.find('@')==std::string_view::npos?
                Prefetch(assets_->request_bytes(source,priority)):
                Prefetch(assets_->request_drawable_resources(source,priority));
            requests_.emplace(source,AssetRequestEntry{std::move(request)});
        } catch(std::exception const& error) {
            failed_.emplace(path);
            warn_prefetch(path,error);
        }
    }
    for(auto it=requests_.begin();it!=requests_.end();) {
        const auto ready=[](auto const& request) { return request.ready(); };
        if(it->second.completed || !std::visit(ready,it->second.request)) {
            ++it;
            continue;
        }
        try {
            std::visit([](auto const& request) { (void)request.get(); },it->second.request);
            it->second.completed=true;
            ++it;
        } catch(std::exception const& error) {
            failed_.insert(it->first);
            warn_prefetch(it->first,error);
            it=requests_.erase(it);
        }
    }
}

void WindowSceneStreaming::update(Scene const& authored,Vector3 position,
    std::string const& room,float seconds,PhysicsScene* physics)
{
    if(!world_)return;
    Vector3 velocity{0.f,0.f,0.f};
    if(previous_valid_ && seconds>=0.0001f && std::isfinite(seconds)) {
        const auto inverse=1.f/seconds;
        velocity.x=std::clamp((position.x-previous_.x)*inverse,-5000.f,5000.f);
        velocity.y=std::clamp((position.y-previous_.y)*inverse,-5000.f,5000.f);
        velocity.z=std::clamp((position.z-previous_.z)*inverse,-5000.f,5000.f);
    }
    world_->update(position,velocity,room,&authored);
    if(residency_) {
        residency_->apply(*world_);
        for(auto const& cell:world_->source().cells) {
            const bool ready=!residency_->has_chunk(cell.name) || residency_->loaded(cell.name);
            if(world_->cell_payload_ready(cell.name)!=ready)
                world_->set_cell_payload_ready(cell.name,ready);
        }
    }
    if(navigation_)navigation_->sync(*world_);
    if(physics)(void)physics->sync_scene_residency(*world_,position);
    prefetch_assets();
    previous_=position;
    previous_valid_=true;
}

WindowStreamingStats WindowSceneStreaming::stats() const
{
    WindowStreamingStats result;
    if(residency_) {
        result.pending_chunks=residency_->pending_cell_count();
        result.decoded_chunks=residency_->resident_cell_count();
    }
    result.requests=requests_.size();
    result.resolved=static_cast<std::size_t>(std::count_if(requests_.begin(),requests_.end(),
        [](auto const& entry) { return entry.second.completed; }));
    result.failed=failed_.size();
    result.navigation_cells=navigation_?navigation_->resident_count():0;
    return result;
}

} // namespace northstarcpp::window_detail
