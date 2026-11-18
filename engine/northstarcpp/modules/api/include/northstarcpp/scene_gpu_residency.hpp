// NorthStarCpp — cross-provider, fence-safe sector GPU ownership contract.
// Providers supply real allocations and destruction callbacks. This class does
// not manufacture GPU resources or pretend CPU bytes are VRAM measurements.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace northstarcpp::scene_gpu {

enum class ResourceKind : std::uint8_t { vertex_buffer, index_buffer, texture, material };

struct ResourceKey {
    ResourceKind kind=ResourceKind::vertex_buffer;
    std::string asset;
    bool operator==(ResourceKey const& other) const noexcept {
        return kind==other.kind && asset==other.asset;
    }
};
struct ResourceHash {
    std::size_t operator()(ResourceKey const& key) const noexcept {
        auto const h=std::hash<std::string>{}(key.asset);
        return h^(static_cast<std::size_t>(key.kind)*0x9e3779b97f4a7c15ull);
    }
};
struct GpuAllocation {
    std::uint64_t handle=0;       // backend-defined, nonzero
    std::uint64_t allocated_bytes=0; // returned by backend allocation
    std::uint64_t upload_fence=0; // upload/transfer must finish before destruction
};
struct ResidencyStats {
    std::size_t sectors=0, allocations=0, shared_allocations=0;
    std::size_t pending_releases=0;
    std::uint64_t allocated_bytes=0, pending_bytes=0;
    std::uint64_t completed_fence=0;
};

struct SectorResourceStats {
    std::size_t resources=0;
    std::uint64_t referenced_bytes=0; // shared resources counted for this owner
    std::uint64_t exclusive_bytes=0;
    std::uint64_t shared_bytes=0;
};

class SectorGpuResidency final {
public:
    using Upload=std::function<GpuAllocation(ResourceKey const&)>;
    using Release=std::function<void(ResourceKey const&, std::uint64_t)>;
    explicit SectorGpuResidency(Upload upload, Release release)
        : upload_(std::move(upload)), release_(std::move(release)) {
        if(!upload_ || !release_)throw std::invalid_argument("GPU callbacks required");
    }

    // Deduplicates keys within a sector. Multiple sectors share one allocation.
    // Upload failure leaves the sector unchanged; already-created allocations
    // are deferred until their transfer fence completes, never leaked.
    void replace_sector(std::string sector, std::vector<ResourceKey> wanted) {
        if(sector.empty())throw std::invalid_argument("empty sector name");
        std::unordered_set<ResourceKey,ResourceHash> seen;
        std::vector<ResourceKey> desired;
        desired.reserve(wanted.size());
        for(auto& key:wanted) {
            if(key.asset.empty())throw std::invalid_argument("empty asset");
            if(seen.insert(key).second)desired.push_back(std::move(key));
        }

        auto const old=sectors_.find(sector);
        std::unordered_set<ResourceKey,ResourceHash> prior;
        if(old!=sectors_.end())
            prior.insert(old->second.begin(),old->second.end());

        // Stage only resources that are not already live, including resources
        // awaiting retirement that can be safely reacquired.
        std::vector<std::pair<ResourceKey,GpuAllocation>> staged;
        // Reserve before invoking GPU allocators: avoid a vector growth
        // failure between a successful upload and ownership registration.
        staged.reserve(desired.size());
        try {
            for(auto const& key:desired) {
                if(allocations_.count(key))continue;
                auto const allocation=upload_(key);
                if(!allocation.handle)
                    throw std::runtime_error("GPU upload returned null handle");
                staged.emplace_back(key,allocation);
            }
        } catch(...) {
            for(auto& entry:staged)
                orphans_.push_back(std::move(entry));
            throw;
        }

        // All provider allocations succeeded. Move ownership in one commit.
        for(auto& entry:staged)
            allocations_.emplace(std::move(entry.first),Record{entry.second,0,entry.second.upload_fence});
        for(auto const& key:desired) {
            if(!prior.count(key))++allocations_.at(key).owners;
        }
        if(old!=sectors_.end()) {
            for(auto const& key:old->second)
                if(!seen.count(key))--allocations_.at(key).owners;
        }
        if(desired.empty())sectors_.erase(sector);
        else sectors_[std::move(sector)]=std::move(desired);
    }

    void unload_sector(std::string const& sector) {
        if(sectors_.count(sector))replace_sector(sector,{});
    }

    // Must be called using the actual graphics/transfer queue fence value
    // submitted by the provider after referencing these resources.
    void mark_submitted(std::string const& sector,std::uint64_t fence) {
        if(fence<completed_fence_)
            throw std::invalid_argument("submitted GPU fence already retired");
        auto const it=sectors_.find(sector);
        if(it==sectors_.end())return;
        for(auto const& key:it->second) {
            auto& record=allocations_.at(key);
            record.last_fence=std::max(record.last_fence,fence);
        }
    }

    // Only callback-released allocations whose upload and last read fences
    // have both completed; never block the render thread in this method.
    std::size_t collect(std::uint64_t completed_fence) {
        if(completed_fence<completed_fence_)
            throw std::invalid_argument("completed fence moved backwards");
        completed_fence_=completed_fence;
        std::size_t count=0;
        for(auto it=allocations_.begin();it!=allocations_.end();) {
            if(it->second.owners!=0 || it->second.last_fence>completed_fence) {
                ++it;continue;
            }
            release_(it->first,it->second.allocation.handle);
            it=allocations_.erase(it);
            ++count;
        }
        for(auto it=orphans_.begin();it!=orphans_.end();) {
            if(it->second.upload_fence>completed_fence) {++it;continue;}
            release_(it->first,it->second.handle);
            it=orphans_.erase(it);
            ++count;
        }
        return count;
    }

    SectorResourceStats sector_stats(std::string const& sector) const {
        SectorResourceStats result;
        auto const owner=sectors_.find(sector);
        if(owner==sectors_.end())return result;
        for(auto const& key:owner->second) {
            auto const& record=allocations_.at(key);
            ++result.resources;
            result.referenced_bytes+=record.allocation.allocated_bytes;
            if(record.owners==1)
                result.exclusive_bytes+=record.allocation.allocated_bytes;
            else
                result.shared_bytes+=record.allocation.allocated_bytes;
        }
        return result;
    }

    ResidencyStats stats() const noexcept {
        ResidencyStats result;
        result.sectors=sectors_.size();
        result.allocations=allocations_.size()+orphans_.size();
        result.completed_fence=completed_fence_;
        for(auto const& item:allocations_) {
            auto const& record=item.second;
            result.allocated_bytes+=record.allocation.allocated_bytes;
            if(record.owners>1)++result.shared_allocations;
            if(record.owners==0) {
                ++result.pending_releases;
                result.pending_bytes+=record.allocation.allocated_bytes;
            }
        }
        for(auto const& item:orphans_) {
            result.allocated_bytes+=item.second.allocated_bytes;
            result.pending_bytes+=item.second.allocated_bytes;
            ++result.pending_releases;
        }
        return result;
    }

private:
    struct Record {
        GpuAllocation allocation{};
        std::size_t owners=0;
        std::uint64_t last_fence=0;
    };
    Upload upload_;
    Release release_;
    std::unordered_map<ResourceKey,Record,ResourceHash> allocations_;
    std::unordered_map<std::string,std::vector<ResourceKey>> sectors_;
    std::vector<std::pair<ResourceKey,GpuAllocation>> orphans_;
    std::uint64_t completed_fence_=0;
};

} // namespace northstarcpp::scene_gpu
