#pragma once
// D3D12 legacy resource-state tracking for Render Graph passes.
// State is scoped to one command-list frame, with explicit known initial
// states; no plugin ABI types escape the provider.
#include <d3d12.h>
#include <cstddef>
#include <unordered_map>

namespace northstarcpp::render::d3d12::detail {
class D3D12ResourceStates final {
public:
    void seed(ID3D12Resource* resource,D3D12_RESOURCE_STATES initial);
    void transition(ID3D12GraphicsCommandList* command,
                    ID3D12Resource* resource,D3D12_RESOURCE_STATES desired);
    D3D12_RESOURCE_STATES state(ID3D12Resource* resource) const;
    bool contains(ID3D12Resource* resource) const noexcept;
    std::size_t emitted_transitions() const noexcept {return emitted_;}
    void reset() noexcept {states_.clear();emitted_=0;}
private:
    std::unordered_map<ID3D12Resource*,D3D12_RESOURCE_STATES> states_;
    std::size_t emitted_=0;
};
} // namespace northstarcpp::render::d3d12::detail
