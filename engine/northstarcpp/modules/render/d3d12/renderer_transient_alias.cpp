#include "renderer_transient_alias.hpp"
#include "../shared/transient_resource_planner.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace northstarcpp::render::d3d12::detail {
namespace {
bool valid_rt(const D3D12_RESOURCE_DESC& d) {
    return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        d.Flags==D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET &&
        d.Format!=DXGI_FORMAT_UNKNOWN && d.Width>0 && d.Height>0 &&
        d.SampleDesc.Count==1 && d.SampleDesc.Quality==0 &&
        d.DepthOrArraySize==1 && d.MipLevels==1 &&
        d.Layout==D3D12_TEXTURE_LAYOUT_UNKNOWN;
}
bool same_desc(const D3D12_RESOURCE_DESC& a,const D3D12_RESOURCE_DESC& b) {
    return a.Dimension==b.Dimension && a.Alignment==b.Alignment &&
        a.Width==b.Width && a.Height==b.Height &&
        a.DepthOrArraySize==b.DepthOrArraySize && a.MipLevels==b.MipLevels &&
        a.Format==b.Format && a.SampleDesc.Count==b.SampleDesc.Count &&
        a.SampleDesc.Quality==b.SampleDesc.Quality &&
        a.Layout==b.Layout && a.Flags==b.Flags;
}
}
void D3D12PlacedTransientAlias::configure(
    ID3D12Device* device,const northstarcpp::render::RenderGraph& graph,
    const northstarcpp::render::RenderGraphPlan& plan,
    const std::vector<D3D12TransientTextureRequest>& requests) {
    if(!device||enabled()||!members_.empty()||requests.empty())
        throw std::invalid_argument("Invalid D3D12 transient alias plan");
    using northstarcpp::render::TransientResourceRequest;
    std::map<std::tuple<int,int,int,int>,std::uint64_t> keys;
    std::vector<TransientResourceRequest> allocations;
    std::vector<Member> requested;
    // A transient alias configuration knows its exact membership up front.
    // Reserve once instead of reallocating both arrays while registering GPU targets.
    allocations.reserve(requests.size());
    requested.reserve(requests.size());
    std::uint64_t original_bytes=0;
    for(auto const& req:requests) {
        auto const& d=req.description;
        if(req.resource>=graph.resources().size()||
           graph.resources()[req.resource].imported||!valid_rt(d))
            throw std::invalid_argument("Unsupported transient render target");
        auto info=device->GetResourceAllocationInfo(0,1,&d);
        if(!info.SizeInBytes||info.SizeInBytes==UINT64_MAX||!info.Alignment)
            throw std::runtime_error("Invalid D3D12 placed resource allocation");
        if(original_bytes>std::numeric_limits<std::uint64_t>::max()-info.SizeInBytes)
            throw std::overflow_error("Transient allocation size overflow");
        original_bytes+=info.SizeInBytes;
        const auto kind=std::make_tuple(static_cast<int>(d.Format),
            static_cast<int>(d.Flags),static_cast<int>(d.SampleDesc.Count),
            static_cast<int>(d.Dimension));
        auto [it,inserted]=keys.emplace(kind,keys.size()+1);
        allocations.push_back({req.resource,info.SizeInBytes,info.Alignment,it->second});
        requested.push_back({req.resource,d,nullptr,-1});
    }
    // The backend-neutral planner rejects imported, duplicate, overlapping,
    // incompatible or malformed resources before any physical allocation.
    const auto plan_slots=northstarcpp::render::plan_transient_resources(
        graph,plan,allocations);
    // Pack independent physical slots at different offsets in ONE heap.
    // Resources within each slot share the same offset only when their
    // lifetimes are disjoint; overlapping slots never overlap physically.
    std::vector<Slot> built;
    built.reserve(plan_slots.slots.size());
    std::uint64_t total=0;
    std::uint64_t heap_alignment=0;
    for(auto const& slot:plan_slots.slots) {
        const auto alignment=slot.alignment;
        if(total>std::numeric_limits<std::uint64_t>::max()-(alignment-1))
            throw std::overflow_error("Transient heap alignment overflow");
        const auto offset=(total+alignment-1)&~(alignment-1);
        if(offset>std::numeric_limits<std::uint64_t>::max()-slot.reserved_bytes)
            throw std::overflow_error("Transient heap total overflow");
        built.push_back({offset,-1});
        total=offset+slot.reserved_bytes;
        heap_alignment=std::max(heap_alignment,alignment);
    }
    D3D12_HEAP_DESC heap_desc{};
    heap_desc.SizeInBytes=total;
    heap_desc.Alignment=heap_alignment;
    heap_desc.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Properties.CreationNodeMask=1;
    heap_desc.Properties.VisibleNodeMask=1;
    heap_desc.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    Microsoft::WRL::ComPtr<ID3D12Heap> allocated;
    if(FAILED(device->CreateHeap(&heap_desc,
           IID_PPV_ARGS(allocated.GetAddressOf()))))
        throw std::runtime_error("CreateHeap failed for transient GPU arena");
    for(auto& member:requested) {
        member.slot=plan_slots.resource_to_slot[member.id];
        if(member.slot<0||static_cast<std::size_t>(member.slot)>=built.size())
            throw std::logic_error("Unbound D3D12 transient allocation");
    }
    heap_=std::move(allocated);
    slots_=std::move(built);
    members_=std::move(requested);
    allocated_bytes_=total;
    saved_bytes_=original_bytes-total;
    barrier_count_=0;
}
void D3D12PlacedTransientAlias::configure(
    ID3D12Device* device,const northstarcpp::render::RenderGraph& graph,
    const northstarcpp::render::RenderGraphPlan& plan,
    northstarcpp::render::ResourceId first,const D3D12_RESOURCE_DESC& first_desc,
    northstarcpp::render::ResourceId second,const D3D12_RESOURCE_DESC& second_desc) {
    configure(device,graph,plan,
        std::vector<D3D12TransientTextureRequest>{{first,first_desc},{second,second_desc}});
    // Preserve the original pair contract: both targets MUST physically alias.
    if(physical_slot(first)!=physical_slot(second)) {
        reset();
        throw std::logic_error("Two-resource Render Graph plan has overlapping lifetimes");
    }
}
int D3D12PlacedTransientAlias::physical_slot(
    northstarcpp::render::ResourceId resource) const {
    for(auto const& member:members_)
        if(member.id==resource)return member.slot;
    throw std::out_of_range("Unknown transient resource ID");
}
void D3D12PlacedTransientAlias::create(
    ID3D12Device* device,northstarcpp::render::ResourceId resource,
    const D3D12_RESOURCE_DESC& desc,const D3D12_CLEAR_VALUE& clear,
    Microsoft::WRL::ComPtr<ID3D12Resource>& output) {
    if(!device||!enabled()||output)
        throw std::invalid_argument("Invalid placed transient resource creation");
    for(auto& member:members_) {
        if(member.id!=resource)continue;
        if(member.resource||!same_desc(desc,member.desc)||clear.Format!=desc.Format)
            throw std::invalid_argument("Placed resource conflicts with its graph allocation");
        auto const& slot=slots_.at(static_cast<std::size_t>(member.slot));
        if(FAILED(device->CreatePlacedResource(heap_.Get(),slot.offset,&desc,
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,
              IID_PPV_ARGS(output.GetAddressOf()))))
            throw std::runtime_error("CreatePlacedResource failed for transient target");
        member.resource=output.Get();
        return;
    }
    throw std::out_of_range("Unplanned transient render target");
}
void D3D12PlacedTransientAlias::activate(
    ID3D12GraphicsCommandList* commands,northstarcpp::render::ResourceId resource) {
    if(!commands||!enabled())throw std::invalid_argument("Invalid transient alias activation");
    for(std::size_t i=0;i<members_.size();++i) {
        auto const& member=members_[i];
        if(member.id!=resource)continue;
        if(!member.resource)
            throw std::logic_error("Uncreated placed resource in Render Graph");
        auto& slot=slots_.at(static_cast<std::size_t>(member.slot));
        if(slot.active==static_cast<int>(i))return;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barrier.Aliasing.pResourceBefore=slot.active<0?
            nullptr:members_[static_cast<std::size_t>(slot.active)].resource;
        barrier.Aliasing.pResourceAfter=member.resource;
        commands->ResourceBarrier(1,&barrier);
        slot.active=static_cast<int>(i);
        ++barrier_count_;
        return;
    }
    throw std::out_of_range("Unknown transient alias activation");
}
void D3D12PlacedTransientAlias::reset() noexcept {
    // The graphics queue must be fenced and external placed-resource
    // ComPtrs released before their parent heaps are destroyed.
    members_.clear();
    slots_.clear();
    heap_.Reset();
    allocated_bytes_=saved_bytes_=barrier_count_=0;
}
}
