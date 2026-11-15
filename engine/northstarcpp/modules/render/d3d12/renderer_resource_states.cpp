#include "renderer_resource_states.hpp"
#include <stdexcept>
namespace northstarcpp::render::d3d12::detail {
void D3D12ResourceStates::seed(ID3D12Resource* resource,D3D12_RESOURCE_STATES initial) {
    if(!resource)throw std::invalid_argument("Cannot track a null GPU resource");
    auto entry=states_.emplace(resource,initial);
    if(!entry.second && entry.first->second!=initial)
        throw std::logic_error("Conflicting GPU resource initial state");
}
D3D12_RESOURCE_STATES D3D12ResourceStates::state(ID3D12Resource* resource) const {
    const auto it=states_.find(resource);
    if(it==states_.end())throw std::logic_error("Untracked D3D12 resource state");
    return it->second;
}
bool D3D12ResourceStates::contains(ID3D12Resource* resource) const noexcept {
    return states_.find(resource)!=states_.end();
}
void D3D12ResourceStates::transition(
    ID3D12GraphicsCommandList* command,ID3D12Resource* resource,
    D3D12_RESOURCE_STATES desired) {
    if(!command)throw std::invalid_argument("Missing GPU command list");
    auto it=states_.find(resource);
    if(it==states_.end())throw std::logic_error("Transition of untracked D3D12 resource");
    const auto before=it->second;
    if(before==desired)return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=resource;
    barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=before;
    barrier.Transition.StateAfter=desired;
    command->ResourceBarrier(1,&barrier);
    it->second=desired;
    ++emitted_;
}
} // namespace northstarcpp::render::d3d12::detail
