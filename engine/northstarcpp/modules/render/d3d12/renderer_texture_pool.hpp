#pragma once
// Fence-safe reuse of matching committed D3D12 render targets across resizes.
// Never aliases resources that are simultaneously alive in the frame graph.
#include <d3d12.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace northstarcpp::render::d3d12::detail {
class D3D12TextureReusePool final {
public:
    static constexpr std::uint64_t budget_bytes=128ull*1024*1024;
    // Call only after the GPU queue has completed all uses of this texture.
    void recycle(ID3D12Device* device,
                 Microsoft::WRL::ComPtr<ID3D12Resource>& target,
                 D3D12_RESOURCE_STATES final_state);
    // Exact desc + optimized clear + state match; otherwise allocates.
    void acquire(ID3D12Device* device,const D3D12_RESOURCE_DESC& description,
                 D3D12_RESOURCE_STATES desired_state,const D3D12_CLEAR_VALUE& clear,
                 Microsoft::WRL::ComPtr<ID3D12Resource>& output,const char* label);
    std::uint64_t cached_bytes() const noexcept {return cached_bytes_;}
    std::uint64_t cache_hits() const noexcept {return hits_;}
    std::size_t count() const noexcept {return entries_.size();}
private:
    struct Entry {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES state;
        D3D12_CLEAR_VALUE clear;
        std::uint64_t bytes;
    };
    std::vector<Entry> entries_;
    // Optimized clear values belong to allocation metadata, not DXGI formats.
    std::unordered_map<ID3D12Resource*,D3D12_CLEAR_VALUE> live_clears_;
    std::uint64_t cached_bytes_=0;
    std::uint64_t hits_=0;
};
} // namespace northstarcpp::render::d3d12::detail
