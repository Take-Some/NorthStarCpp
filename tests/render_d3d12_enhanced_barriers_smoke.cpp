#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "../engine/northstarcpp/modules/render/d3d12/renderer_enhanced_barriers.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
#define REQUIRE(c) do {if(!(c)) throw std::runtime_error("Enhanced Barrier QA: " #c);} while(false)

int main(int argc,char** argv) {
    const bool hardware=argc==2 && std::string(argv[1])=="--hardware";
    if(argc>2 || (argc==2&&!hardware))return 2;
    using namespace northstarcpp::render;
    using namespace northstarcpp::render::d3d12::detail;
    using Microsoft::WRL::ComPtr;
    REQUIRE(enhanced_usage(ResourceAccess::Present).layout==D3D12_BARRIER_LAYOUT_PRESENT);
    REQUIRE(enhanced_usage(ResourceAccess::ShaderRead).layout==D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
    REQUIRE(enhanced_usage(ResourceAccess::DepthWrite).layout==D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
    REQUIRE(enhanced_usage(ResourceAccess::CopyDestination).access==D3D12_BARRIER_ACCESS_COPY_DEST);
    ComPtr<IDXGIFactory4> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))return 77;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12Device> device;
    if(hardware) {
        for(UINT i=0;;++i) {
            ComPtr<IDXGIAdapter1> candidate;
            if(FAILED(factory->EnumAdapters1(i,candidate.GetAddressOf())))break;
            DXGI_ADAPTER_DESC1 desc{};
            candidate->GetDesc1(&desc);
            if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
            if(SUCCEEDED(D3D12CreateDevice(candidate.Get(),D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(device.GetAddressOf())))) {
                candidate.As(&adapter);break;
            }
        }
        if(!device)return 77;
    }else {
        if(FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(adapter.GetAddressOf()))))
            return 77;
        if(FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,
              IID_PPV_ARGS(device.GetAddressOf()))))return 77;
    }
    if(!enhanced_barriers_supported(device.Get())) {
        std::puts("D3D12 Enhanced Barriers unsupported by this runtime/adapter");
        return 77;
    }
    ComPtr<ID3D12CommandAllocator> alloc;
    REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(alloc.GetAddressOf()))));
    ComPtr<ID3D12GraphicsCommandList> base;
    REQUIRE(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,
        alloc.Get(),nullptr,IID_PPV_ARGS(base.GetAddressOf()))));
    ComPtr<ID3D12GraphicsCommandList7> enhanced;
    if(FAILED(base.As(&enhanced)))return 77;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=64;desc.Height=64;desc.DepthOrArraySize=1;desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};clear.Format=desc.Format;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask=1;heap.VisibleNodeMask=1;
    ComPtr<ID3D12Resource> texture;
    REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,
        &desc,D3D12_RESOURCE_STATE_COMMON,&clear,
        IID_PPV_ARGS(texture.GetAddressOf()))));
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&hd,
        IID_PPV_ARGS(rtv_heap.GetAddressOf()))));
    auto rtv=rtv_heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(texture.Get(),nullptr,rtv);
    auto to_rtv=enhanced_texture_barrier(texture.Get(),
        {0,ResourceAccess::Present,ResourceAccess::ColorAttachment});
    REQUIRE(to_rtv.Subresources.IndexOrFirstMipLevel==UINT32_MAX);
    D3D12_BARRIER_GROUP group{};
    group.Type=D3D12_BARRIER_TYPE_TEXTURE;
    group.NumBarriers=1;
    group.pTextureBarriers=&to_rtv;
    enhanced->Barrier(1,&group);
    const float color[4]{0.2f,0.4f,0.6f,1.f};
    enhanced->ClearRenderTargetView(rtv,color,0,nullptr);
    auto to_common=enhanced_texture_barrier(texture.Get(),
        {0,ResourceAccess::ColorAttachment,ResourceAccess::Present});
    group.pTextureBarriers=&to_common;
    enhanced->Barrier(1,&group);
    REQUIRE(SUCCEEDED(enhanced->Close()));
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    REQUIRE(SUCCEEDED(device->CreateCommandQueue(&qd,
        IID_PPV_ARGS(queue.GetAddressOf()))));
    ID3D12CommandList* lists[]{enhanced.Get()};
    queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;
    REQUIRE(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(fence.GetAddressOf()))));
    REQUIRE(SUCCEEDED(queue->Signal(fence.Get(),1)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    REQUIRE(event!=nullptr);
    if(fence->GetCompletedValue()<1) {
        REQUIRE(SUCCEEDED(fence->SetEventOnCompletion(1,event)));
        REQUIRE(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    }
    CloseHandle(event);
    REQUIRE(device->GetDeviceRemovedReason()==S_OK);
    std::printf("D3D12 Enhanced Barriers: %s execute/barrier/clear/fence passed\n",
        hardware?"hardware":"WARP");
}
