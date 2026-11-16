#pragma once
// Pure CPU render-graph transient allocation planner.
// No GPU API types or engine ABI changes; execution backends may choose
// whether physical aliasing is safe for the resulting plan.
#include "render_graph.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace northstarcpp::render {
struct TransientResourceRequest {
    ResourceId resource;
    std::uint64_t bytes;
    std::uint64_t alignment;
    std::uint64_t compatibility_key;
};
struct TransientMemorySlot {
    std::uint64_t reserved_bytes=0;
    std::uint64_t alignment=0;
    std::uint64_t compatibility_key=0;
    int last_use=-1;
};
struct TransientAllocationPlan {
    std::vector<int> resource_to_slot;
    std::vector<TransientMemorySlot> slots;
    std::uint64_t committed_bytes=0;
    std::uint64_t peak_live_bytes=0;
};
inline TransientAllocationPlan plan_transient_resources(
    const RenderGraph& graph, const RenderGraphPlan& execution,
    const std::vector<TransientResourceRequest>& requests) {
    if(execution.lifetimes.size()!=graph.resources().size())
        throw std::invalid_argument("Render graph lifetime count mismatch");
    TransientAllocationPlan result;
    result.resource_to_slot.assign(graph.resources().size(),-1);
    std::vector<TransientResourceRequest> ordered=requests;
    // Direct resource indexing avoids per-entry hash-node allocations.
    std::vector<std::uint8_t> seen(graph.resources().size(), 0);
    for(const auto& req:ordered) {
        if(req.resource>=seen.size() || seen[req.resource])
            throw std::invalid_argument("Invalid or duplicate transient resource");
        seen[req.resource]=1;
        if(graph.resources()[req.resource].imported)
            throw std::invalid_argument("Imported resources must not alias");
        if(req.bytes==0||req.alignment==0||
           (req.alignment&(req.alignment-1))!=0||
           req.bytes>std::numeric_limits<std::uint64_t>::max()-(req.alignment-1))
            throw std::invalid_argument("Invalid transient allocation description");
        const auto life=execution.lifetimes[req.resource];
        if(life.first<0 || life.second<life.first ||
           static_cast<std::size_t>(life.second)>=execution.passes.size())
            throw std::invalid_argument("Invalid transient resource lifetime");
    }
    std::sort(ordered.begin(),ordered.end(),[&](const auto& a,const auto& b) {
        const auto la=execution.lifetimes[a.resource];
        const auto lb=execution.lifetimes[b.resource];
        return la.first==lb.first?a.resource<b.resource:la.first<lb.first;
    });
    for(const auto& req:ordered) {
        const auto life=execution.lifetimes[req.resource];
        const auto aligned=(req.bytes+req.alignment-1)&~(req.alignment-1);
        int slot=-1;
        // A slot is reusable only when the earlier pass is strictly before
        // the new first use. Read/write use in the same pass always overlaps.
        for(std::size_t i=0;i<result.slots.size();++i) {
            const auto& current=result.slots[i];
            if(current.last_use<life.first &&
               current.compatibility_key==req.compatibility_key &&
               current.alignment==req.alignment &&
               current.reserved_bytes>=aligned) {
                slot=static_cast<int>(i);break;
            }
        }
        if(slot<0) {
            slot=static_cast<int>(result.slots.size());
            result.slots.push_back({aligned,req.alignment,req.compatibility_key,life.second});
        }else result.slots[slot].last_use=life.second;
        result.resource_to_slot[req.resource]=slot;
    }
    for(const auto& slot:result.slots) {
        if(result.committed_bytes>std::numeric_limits<std::uint64_t>::max()-slot.reserved_bytes)
            throw std::overflow_error("Transient memory total overflow");
        result.committed_bytes+=slot.reserved_bytes;
    }
    // Each slot may be reused only after the previous lifetime ended.
    // Account for its physical allocation once per live interval. A single
    // event array replaces the passes x slots x requests nested scan.
    // End events are applied before begin events at the next pass, so a
    // strictly non-overlapping handoff does not inflate the measured peak.
    struct LiveEvents { std::uint64_t begin=0, end=0; };
    std::vector<LiveEvents> events(execution.passes.size());
    for(const auto& req:ordered) {
        const auto life=execution.lifetimes[req.resource];
        const auto slot=result.resource_to_slot[req.resource];
        const auto bytes=result.slots[static_cast<std::size_t>(slot)].reserved_bytes;
        auto& begin=events[static_cast<std::size_t>(life.first)].begin;
        if(begin>std::numeric_limits<std::uint64_t>::max()-bytes)
            throw std::overflow_error("Transient live bytes overflow");
        begin+=bytes;
        const auto after=static_cast<std::size_t>(life.second)+1;
        if(after<events.size()) {
            auto& end=events[after].end;
            if(end>std::numeric_limits<std::uint64_t>::max()-bytes)
                throw std::overflow_error("Transient released bytes overflow");
            end+=bytes;
        }
    }
    std::uint64_t live=0;
    for(const auto& event:events) {
        if(event.end>live)
            throw std::logic_error("Invalid transient slot lifetime overlap");
        live-=event.end;
        if(event.begin>std::numeric_limits<std::uint64_t>::max()-live)
            throw std::overflow_error("Transient peak bytes overflow");
        live+=event.begin;
        result.peak_live_bytes=std::max(result.peak_live_bytes,live);
    }
    return result;
}
}
