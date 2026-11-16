#include "renderer_private.hpp"
#include "../../shared/frame_memory_counters.hpp"

namespace northstarcpp::render::d3d12::detail {
namespace {
constexpr UINT64 page_size=4ull*1024*1024;
constexpr UINT64 idle_budget=32ull*1024*1024;
constexpr UINT64 placement_alignment=D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;

UINT64 align_up(UINT64 value,UINT64 alignment)
{
    if(value>UINT64_MAX-(alignment-1))
        throw std::length_error("D3D12 upload size overflow");
    return (value+alignment-1)&~(alignment-1);
}
}

UploadPages::Batch::Batch(UploadPages& pool,ID3D12Device* device,UINT64 completed)
    : pool_(pool),device_(device)
{
    if(!device_ || completed==UINT64_MAX)
        throw std::runtime_error("D3D12 upload queue is unavailable");
    if(pool_.batch_active_)
        throw std::logic_error("D3D12 upload page batch already active");
    pool_.reclaim(completed);
    pool_.batch_active_=true;
}

UploadPages::Batch::~Batch() noexcept
{
    if(!retired_)
        for(auto& page:pool_.pages_)
            if(page.reserved) {
                page.reserved=false;
                page.used=0;
            }
    pool_.batch_active_=false;
}

UploadSlice UploadPages::Batch::allocate(UINT64 bytes)
{
    if(retired_ || !bytes)
        throw std::invalid_argument("Invalid D3D12 upload page request");
    return pool_.allocate(device_,bytes);
}

void UploadPages::Batch::retire(UINT64 fence_value) noexcept
{
    for(auto& page:pool_.pages_)
        if(page.reserved) {
            page.reserved=false;
            page.fence=fence_value;
        }
    retired_=true;
}

UploadPages::~UploadPages() noexcept
{
    for(auto& page:pages_)
        if(page.mapped)page.resource->Unmap(0,nullptr);
}

void UploadPages::reclaim(UINT64 completed) noexcept
{
    UINT64 idle=0;
    for(auto it=pages_.begin();it!=pages_.end();) {
        auto& page=*it;
        if(page.fence>completed) { ++it; continue; }
        page.fence=page.used=0;
        if(page.capacity>idle_budget-idle) {
            page.resource->Unmap(0,nullptr);
            it=pages_.erase(it);
        } else {
            idle+=page.capacity;
            ++it;
        }
    }
}

UploadSlice UploadPages::allocate(ID3D12Device* device,UINT64 bytes)
{
    Page* selected=nullptr;
    for(auto& page:pages_) {
        if(page.fence)continue;
        const auto offset=align_up(page.used,placement_alignment);
        if(offset<=page.capacity && bytes<=page.capacity-offset) {
            selected=&page;
            if(page.reserved)break; // Pack the current batch before taking a new page.
        }
    }
    if(!selected) {
        Page page;
        page.capacity=std::max(page_size,align_up(bytes,D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));
        const auto upload=heap(D3D12_HEAP_TYPE_UPLOAD);
        const auto desc=memory(page.capacity);
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&page.resource)),
            "Create D3D12 upload page");
        const D3D12_RANGE no_reads{};
        check(page.resource->Map(0,&no_reads,reinterpret_cast<void**>(&page.mapped)),
              "Map D3D12 upload page");
        pages_.push_back(std::move(page));
        selected=&pages_.back();
        diagnostics::frame_memory::device_allocated(selected->capacity);
    }

    const auto offset=align_up(selected->used,placement_alignment);
    selected->used=offset+bytes;
    selected->reserved=true;
    return {selected->resource.Get(),selected->mapped+offset,offset};
}

} // namespace northstarcpp::render::d3d12::detail
