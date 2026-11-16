#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

namespace northstarcpp::render::d3d12::detail {

struct UploadSlice {
    ID3D12Resource* resource=nullptr;
    unsigned char* data=nullptr;
    UINT64 offset=0;
};

// Persistently mapped pages are owned by the renderer. A batch shares pages
// between uploads and either retires them against the queue or releases the
// untouched reservation when validation/recording aborts.
class UploadPages {
public:
    class Batch {
    public:
        Batch(UploadPages& pool,ID3D12Device* device,UINT64 completed);
        ~Batch() noexcept;
        Batch(Batch const&)=delete;
        Batch& operator=(Batch const&)=delete;
        UploadSlice allocate(UINT64 bytes);
        void retire(UINT64 fence_value) noexcept;

    private:
        UploadPages& pool_;
        ID3D12Device* device_;
        bool retired_=false;
    };

    ~UploadPages() noexcept;
    UploadPages()=default;
    UploadPages(UploadPages const&)=delete;
    UploadPages& operator=(UploadPages const&)=delete;

private:
    struct Page {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        unsigned char* mapped=nullptr;
        UINT64 capacity=0,used=0,fence=0;
        bool reserved=false;
    };
    void reclaim(UINT64 completed) noexcept;
    UploadSlice allocate(ID3D12Device* device,UINT64 bytes);

    std::vector<Page> pages_;
    bool batch_active_=false;
};

} // namespace northstarcpp::render::d3d12::detail
