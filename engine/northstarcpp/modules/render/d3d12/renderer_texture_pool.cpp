#include "renderer_texture_pool.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
namespace northstarcpp::render::d3d12::detail {
namespace {
bool same_desc(const D3D12_RESOURCE_DESC& a,const D3D12_RESOURCE_DESC& b) {
    // Driver resource descriptors may materialize the 64 KiB default
    // placement alignment even when the creation descriptor used zero.
    auto alignment=[](const D3D12_RESOURCE_DESC& d) {
        if(d.Alignment)return d.Alignment;
        return d.SampleDesc.Count>1?
            static_cast<UINT64>(D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT):
            static_cast<UINT64>(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    };
    return a.Dimension==b.Dimension && alignment(a)==alignment(b) &&
           a.Width==b.Width && a.Height==b.Height &&
           a.DepthOrArraySize==b.DepthOrArraySize &&
           a.MipLevels==b.MipLevels && a.Format==b.Format &&
           a.SampleDesc.Count==b.SampleDesc.Count &&
           a.SampleDesc.Quality==b.SampleDesc.Quality &&
           a.Layout==b.Layout && a.Flags==b.Flags;
}
bool same_clear(const D3D12_CLEAR_VALUE& a,const D3D12_CLEAR_VALUE& b,
                bool depth) {
    if(a.Format!=b.Format)return false;
    if(depth)return a.DepthStencil.Depth==b.DepthStencil.Depth &&
                    a.DepthStencil.Stencil==b.DepthStencil.Stencil;
    for(int i=0;i<4;++i)if(a.Color[i]!=b.Color[i])return false;
    return true;
}
std::uint64_t resource_bytes(ID3D12Device* device,const D3D12_RESOURCE_DESC& desc) {
    return device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
}
}
void D3D12TextureReusePool::recycle(
    ID3D12Device* device,Microsoft::WRL::ComPtr<ID3D12Resource>& target,
    D3D12_RESOURCE_STATES final_state) {
    if(!target)return;
    if(!device)throw std::invalid_argument("Missing D3D12 device for recycling");
    const auto desc=target->GetDesc();
    auto metadata=live_clears_.find(target.Get());
    if(metadata==live_clears_.end()) {
        target.Reset();return; // do not cache an allocation of unknown origin
    }
    const D3D12_CLEAR_VALUE original=metadata->second;
    live_clears_.erase(metadata);
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       !(desc.Flags&(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|
                    D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))) {
        target.Reset();return;
    }
    const auto bytes=resource_bytes(device,desc);
    if(bytes==0||bytes>budget_bytes) {target.Reset();return;}
    // Keep only the most recent compatible resources within a fixed budget.
    while(cached_bytes_+bytes>budget_bytes && !entries_.empty()) {
        cached_bytes_-=entries_.front().bytes;
        entries_.erase(entries_.begin());
    }
    if(cached_bytes_+bytes>budget_bytes) {target.Reset();return;}
    Entry entry{};
    entry.resource.Attach(target.Detach());
    entry.state=final_state;
    entry.clear=original;
    entry.bytes=bytes;
    cached_bytes_+=bytes;
    entries_.push_back(std::move(entry));
}
void D3D12TextureReusePool::acquire(
    ID3D12Device* device,const D3D12_RESOURCE_DESC& desc,
    D3D12_RESOURCE_STATES desired_state,const D3D12_CLEAR_VALUE& clear,
    Microsoft::WRL::ComPtr<ID3D12Resource>& output,const char* label) {
    if(!device||!label||output)
        throw std::invalid_argument("Invalid render target pool allocation");
    for(auto it=entries_.begin();it!=entries_.end();++it) {
        if(it->state!=desired_state ||
           !same_desc(it->resource->GetDesc(),desc)||
           !same_clear(it->clear,clear,
                       (desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)!=0))
            continue;
        output.Attach(it->resource.Detach());
        live_clears_[output.Get()]=it->clear;
        cached_bytes_-=it->bytes;
        entries_.erase(it);
        ++hits_;
        return;
    }
    D3D12_HEAP_PROPERTIES memory{};
    memory.Type=D3D12_HEAP_TYPE_DEFAULT;
    memory.CreationNodeMask=1;
    memory.VisibleNodeMask=1;
    const auto result=device->CreateCommittedResource(
        &memory,D3D12_HEAP_FLAG_NONE,&desc,desired_state,&clear,
        IID_PPV_ARGS(output.GetAddressOf()));
    if(SUCCEEDED(result))live_clears_[output.Get()]=clear;
    if(FAILED(result)) {
        char message[200]{};
        std::snprintf(message,sizeof(message),"%s (0x%08lx)",
                      label,static_cast<unsigned long>(result));
        throw std::runtime_error(message);
    }
}
} // namespace northstarcpp::render::d3d12::detail
