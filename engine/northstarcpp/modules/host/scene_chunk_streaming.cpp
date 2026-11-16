// Async NEF8 v2 streaming-cell residency coordinated with SceneWorld.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/scene_chunk_index.hpp>
#include <northstarcpp/scene_world.hpp>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace northstarcpp {
namespace {
std::size_t checked_inflight(std::size_t value) {
    if(value==0 || value>16)
        throw std::invalid_argument("Scene cell in-flight budget must be 1..16");
    return value;
}
}

SceneCellAsyncResidency::SceneCellAsyncResidency(
    std::filesystem::path file,std::size_t max_in_flight)
    : source_(std::make_shared<SceneChunkReader>(std::move(file))),
      available_(source_->chunks()),
      max_in_flight_(checked_inflight(max_in_flight)) {}

SceneCellAsyncResidency::SceneCellAsyncResidency(
    AssetStore const& assets,std::string virtual_path,
    std::size_t max_in_flight)
    : source_(std::make_shared<SceneChunkReader>(assets,std::move(virtual_path))),
      available_(source_->chunks()),
      max_in_flight_(checked_inflight(max_in_flight)) {}

void SceneCellAsyncResidency::apply(SceneWorld const& world)
{
    // Poll active I/O even when the planner reused its cell selection.
    const auto desired=[&](std::string const& cell) {
        return cell.empty() || world.cell_loaded(cell);
    };
    for(auto it=resident_.begin();it!=resident_.end();)
        if(!desired(it->first))it=resident_.erase(it);
        else ++it;
    for(auto it=errors_.begin();it!=errors_.end();)
        if(!desired(it->first))it=errors_.erase(it);
        else ++it;

    for(auto it=pending_.begin();it!=pending_.end();) {
        auto& [cell,job]=*it;
        if(job.wait_for(std::chrono::seconds(0))!=std::future_status::ready) {
            ++it;
            continue;
        }
        try {
            auto records=job.get();
            if(desired(cell))resident_.insert_or_assign(cell,std::move(records));
        } catch(std::exception const& error) {
            if(desired(cell))errors_.insert_or_assign(cell,error.what());
        } catch(...) {
            if(desired(cell))errors_.insert_or_assign(cell,"Unknown Scene cell I/O error");
        }
        it=pending_.erase(it);
    }

    // Root and newly selected cells have priority, followed by file order.
    // Existing work is keyed by cell, so duplicate scheduling calls are cheap.
    const auto schedule=[&](std::string const& cell) {
        if(!desired(cell) || resident_.count(cell) || pending_.count(cell) || errors_.count(cell))
            return;
        pending_.emplace(cell,std::async(std::launch::async,
            [current=source_,cell] { return current->load(cell); }));
    };
    if(pending_.size()>=max_in_flight_)return;
    const auto root=std::find_if(available_.begin(),available_.end(),
        [](auto const& chunk) { return chunk.cell.empty(); });
    if(root!=available_.end())schedule(root->cell);
    for(auto const& transition:world.cell_transitions()) {
        if(pending_.size()>=max_in_flight_)return;
        if(transition.loaded && has_chunk(transition.name))schedule(transition.name);
    }
    for(auto const& chunk:available_) {
        if(pending_.size()>=max_in_flight_)break;
        schedule(chunk.cell);
    }
}

bool SceneCellAsyncResidency::loaded(std::string const& cell) const {
    return resident_.count(cell)!=0;
}

bool SceneCellAsyncResidency::has_chunk(std::string const& cell) const {
    return std::any_of(available_.begin(),available_.end(),
        [&](SceneChunkInfo const& info){return info.cell==cell;});
}

std::vector<SceneChunkRecord> const*
SceneCellAsyncResidency::records(std::string const& cell) const {
    auto it=resident_.find(cell);
    return it==resident_.end()?nullptr:&it->second;
}

std::string SceneCellAsyncResidency::error(std::string const& cell) const {
    auto it=errors_.find(cell);
    return it==errors_.end()?std::string{}:it->second;
}
} // namespace northstarcpp
