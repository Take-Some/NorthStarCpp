#pragma once
// N-resource D3D12 placed-resource allocator, driven by RenderGraph lifetimes.
// Each physical slot owns one DEFAULT heap. Any number of non-overlapping
// logical RT textures may share that slot, with explicit alias barriers.
#include "../shared/render_graph.hpp"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

namespace northstarcpp::render::d3d12::detail {
struct D3D12TransientTextureRequest {
    northstarcpp::render::ResourceId resource;
    D3D12_RESOURCE_DESC description;
};
class D3D12PlacedTransientAlias final {
public:
    // General N-resource API: uses as many heaps as the lifetime/format
    // compatibility planner needs, but permits N resources per heap.
    void configure(ID3D12Device* device,
                   const northstarcpp::render::RenderGraph& graph,
                   const northstarcpp::render::RenderGraphPlan& plan,
                   const std::vector<D3D12TransientTextureRequest>& requests);
    // Source-compatible convenience overload for the initial two-target path.
    void configure(ID3D12Device* device,
                   const northstarcpp::render::RenderGraph& graph,
                   const northstarcpp::render::RenderGraphPlan& plan,
                   northstarcpp::render::ResourceId first,
                   const D3D12_RESOURCE_DESC& first_desc,
                   northstarcpp::render::ResourceId second,
                   const D3D12_RESOURCE_DESC& second_desc);
    void create(ID3D12Device* device,northstarcpp::render::ResourceId resource,
                const D3D12_RESOURCE_DESC& desc,const D3D12_CLEAR_VALUE& clear,
                Microsoft::WRL::ComPtr<ID3D12Resource>& output);
    void activate(ID3D12GraphicsCommandList* commands,
                  northstarcpp::render::ResourceId resource);
    void reset() noexcept;
    bool enabled() const noexcept {return heap_!=nullptr;}
    std::uint64_t allocated_bytes() const noexcept {return allocated_bytes_;}
    std::uint64_t saved_bytes() const noexcept {return saved_bytes_;}
    std::uint64_t barrier_count() const noexcept {return barrier_count_;}
    std::size_t heap_count() const noexcept {return enabled()?1u:0u;}
    std::size_t slot_count() const noexcept {return slots_.size();}
    std::size_t resource_count() const noexcept {return members_.size();}
    int physical_slot(northstarcpp::render::ResourceId resource) const;
private:
    struct Slot {
        std::uint64_t offset=0;
        int active=-1;
    };
    struct Member {
        northstarcpp::render::ResourceId id;
        D3D12_RESOURCE_DESC desc;
        ID3D12Resource* resource=nullptr; // Borrowed; caller owns the placed resource.
        int slot=-1;
    };
    Microsoft::WRL::ComPtr<ID3D12Heap> heap_;
    std::vector<Slot> slots_;
    std::vector<Member> members_;
    std::uint64_t allocated_bytes_=0,saved_bytes_=0,barrier_count_=0;
};
}
