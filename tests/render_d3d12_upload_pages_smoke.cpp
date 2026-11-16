#include "../engine/northstarcpp/modules/render/d3d12/renderer_upload_pages.hpp"
#include <stdexcept>

namespace {
void require(bool value)
{
    if(!value)throw std::runtime_error("D3D12 upload page lifetime mismatch");
}
}

int main()
{
    using namespace northstarcpp::render::d3d12::detail;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if(FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&device))))
        return 77;

    UploadPages pages;
    UploadSlice first;
    {
        UploadPages::Batch batch(pages,device.Get(),0);
        first=batch.allocate(700);
        const auto second=batch.allocate(500);
        require(first.resource && first.data && first.offset==0 &&
            first.resource==second.resource && second.offset==1024);
        batch.retire(3);
    }
    {
        UploadPages::Batch batch(pages,device.Get(),2);
        require(batch.allocate(700).resource!=first.resource);
        batch.retire(5);
    }
    {
        UploadPages::Batch batch(pages,device.Get(),3);
        const auto reused=batch.allocate(700);
        require(reused.resource==first.resource && reused.offset==0);
        // Abandon staging, as on validation failure before ExecuteCommandLists.
    }
    {
        UploadPages::Batch batch(pages,device.Get(),4);
        const auto reused=batch.allocate(700);
        require(reused.resource==first.resource && reused.offset==0);
    }
}
