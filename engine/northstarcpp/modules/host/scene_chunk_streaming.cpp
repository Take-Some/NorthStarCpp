// Async NEF8 v2 streaming-cell residency coordinated with SceneWorld.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/scene_chunk_index.hpp>
#include <northstarcpp/scene_world.hpp>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <unordered_set>
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
    // Work off persistent world residency, not only its one-frame transitions;
    // a pending asynchronous load must remain requested across later frames.
    auto const& index=available_;
    std::unordered_set<std::string> desired{""};
    for(auto const& chunk:index)
        if(chunk.cell.empty() || world.cell_loaded(chunk.cell))
            desired.insert(chunk.cell);

    // Do not hold inactive decoded cells. Pending work is allowed to complete
    // without cancellation blocking the frame; stale results are discarded.
    for(auto it=resident_.begin();it!=resident_.end();)
        if(!desired.count(it->first))it=resident_.erase(it);
        else ++it;
    for(auto it=errors_.begin();it!=errors_.end();)
        if(!desired.count(it->first))it=errors_.erase(it);
        else ++it;

    std::vector<std::string> finished;
    for(auto& [cell,job]:pending_) {
        if(job.wait_for(std::chrono::seconds(0))!=std::future_status::ready)
            continue;
        try {
            auto records=job.get();
            if(desired.count(cell))
                resident_.insert_or_assign(cell,std::move(records));
        } catch(std::exception const& error) {
            if(desired.count(cell))errors_.insert_or_assign(cell,error.what());
        } catch(...) {
            if(desired.count(cell))errors_.insert_or_assign(cell,"Unknown Scene cell I/O error");
        }
        finished.push_back(cell);
    }
    for(auto const& cell:finished)pending_.erase(cell);

    // First schedule forced/near cells in SceneWorld's transition order. The
    // remaining persistent requests are scheduled in deterministic file order.
    std::vector<std::string> ordered;
    ordered.reserve(index.size());
    ordered.push_back("");
    for(auto const& transition:world.cell_transitions())
        if(transition.loaded && desired.count(transition.name))
            ordered.push_back(transition.name);
    for(auto const& chunk:index)
        if(desired.count(chunk.cell))
            ordered.push_back(chunk.cell);
    std::unordered_set<std::string> seen;
    auto current=source_;
    for(auto const& cell:ordered) {
        if(pending_.size()>=max_in_flight_)break;
        if(!seen.insert(cell).second || !desired.count(cell) ||
           resident_.count(cell) || pending_.count(cell) || errors_.count(cell))
            continue;
        pending_.emplace(cell,std::async(std::launch::async,
            [current,cell] {return current->load(cell);}));
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
