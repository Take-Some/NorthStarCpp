#pragma once
// Pure CPU render-graph transient allocation planner.
// No GPU API types or engine ABI changes; execution backends may choose
// whether physical aliasing is safe for the resulting plan.
#include "render_graph.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>
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
    std::unordered_set<ResourceId> seen;
    for(const auto& req:ordered) {
        if(req.resource>=graph.resources().size()||!seen.insert(req.resource).second)
            throw std::invalid_argument("Invalid or duplicate transient resource");
        if(graph.resources()[req.resource].imported)
            throw std::invalid_argument("Imported resources must not alias");
        if(req.bytes==0||req.alignment==0||
           (req.alignment&(req.alignment-1))!=0||
           req.bytes>std::numeric_limits<std::uint64_t>::max()-(req.alignment-1))
            throw std::invalid_argument("Invalid transient allocation description");
        if(execution.lifetimes[req.resource].first<0)
            throw std::invalid_argument("Unused transient resource requested");
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
    // Peak counts physical memory per occupied slot, not logical resource
    // bytes. It is a conservative planning metric (not GPU residency).
    for(std::size_t step=0;step<execution.passes.size();++step) {
        std::uint64_t live=0;
        for(std::size_t slot=0;slot<result.slots.size();++slot) {
            bool in_use=false;
            for(const auto& req:ordered) {
                if(result.resource_to_slot[req.resource]!=static_cast<int>(slot))continue;
                const auto lifetime=execution.lifetimes[req.resource];
                if(lifetime.first<=static_cast<int>(step) && lifetime.second>=static_cast<int>(step)) {
                    in_use=true;break;
                }
            }
            if(in_use)live+=result.slots[slot].reserved_bytes;
        }
        result.peak_live_bytes=std::max(result.peak_live_bytes,live);
    }
    return result;
}
}
