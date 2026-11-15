#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "../engine/northstarcpp/modules/render/d3d12/renderer_transient_alias.hpp"
#include "../engine/northstarcpp/modules/render/d3d12/renderer_pass_graph.hpp"
#include "../engine/northstarcpp/modules/render/d3d12/renderer_resource_states.hpp"
#include <stdexcept>
#include <cstdio>
#define REQUIRE(c) do { if(!(c))     throw std::runtime_error("D3D12 transient alias acceptance: " #c); } while(false)

int main() {
    using Microsoft::WRL::ComPtr;
    using namespace northstarcpp::render::d3d12::detail;
    ComPtr<IDXGIFactory4> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))return 77;
    ComPtr<IDXGIAdapter> adapter;
    if(FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(adapter.GetAddressOf()))))return 77;
    ComPtr<ID3D12Device> device;
    if(FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,
                              IID_PPV_ARGS(device.GetAddressOf()))))return 77;
    const auto graph=build_frame_graph();
    const auto plan=graph.compile();
    auto first=static_cast<northstarcpp::render::ResourceId>(FrameResource::GBufferNormal);
    auto second=static_cast<northstarcpp::render::ResourceId>(FrameResource::Bloom);
    REQUIRE(plan.lifetimes[first].second<plan.lifetimes[second].first);
    D3D12_RESOURCE_DESC normal{};
    normal.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    normal.Width=192;normal.Height=128;
    normal.DepthOrArraySize=1;normal.MipLevels=1;
    normal.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    normal.SampleDesc.Count=1;
    normal.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    auto bloom=normal;
    bloom.Width=48;bloom.Height=32;
    D3D12PlacedTransientAlias allocator;
    allocator.configure(device.Get(),graph,plan,first,normal,second,bloom);
    REQUIRE(allocator.enabled());
    REQUIRE(allocator.allocated_bytes()>0);
    REQUIRE(allocator.saved_bytes()>0);
    D3D12_CLEAR_VALUE clear{};
    clear.Format=normal.Format;
    ComPtr<ID3D12Resource> normal_texture,bloom_texture;
    allocator.create(device.Get(),first,normal,clear,normal_texture);
    allocator.create(device.Get(),second,bloom,clear,bloom_texture);
    REQUIRE(normal_texture.Get()!=bloom_texture.Get());
    bool rejected=false;
    try{
        ComPtr<ID3D12Resource> duplicate;
        allocator.create(device.Get(),first,normal,clear,duplicate);
    }catch(const std::invalid_argument&){rejected=true;}
    REQUIRE(rejected);
    ComPtr<ID3D12DescriptorHeap> rtvs;
    D3D12_DESCRIPTOR_HEAP_DESC heapdesc{};
    heapdesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapdesc.NumDescriptors=2;
    REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&heapdesc,IID_PPV_ARGS(rtvs.GetAddressOf()))));
    auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(normal_texture.Get(),nullptr,rtv);
    auto bloom_rtv=rtv;
    bloom_rtv.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device->CreateRenderTargetView(bloom_texture.Get(),nullptr,bloom_rtv);
    ComPtr<ID3D12CommandAllocator> cmd_allocator;
    REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(cmd_allocator.GetAddressOf()))));
    ComPtr<ID3D12GraphicsCommandList> commands;
    REQUIRE(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,
        cmd_allocator.Get(),nullptr,IID_PPV_ARGS(commands.GetAddressOf()))));
    D3D12ResourceStates states;
    states.seed(normal_texture.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    states.seed(bloom_texture.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    const float normal_clear[4]{0.3f,0.6f,0.9f,1.f};
    const float bloom_clear[4]{0.7f,0.2f,0.1f,1.f};
    allocator.activate(commands.Get(),first);
    states.transition(commands.Get(),normal_texture.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands->ClearRenderTargetView(rtv,normal_clear,0,nullptr);
    states.transition(commands.Get(),normal_texture.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    allocator.activate(commands.Get(),second);
    states.transition(commands.Get(),bloom_texture.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands->ClearRenderTargetView(bloom_rtv,bloom_clear,0,nullptr);
    states.transition(commands.Get(),bloom_texture.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // Next frame: the same heap switches back from Bloom to G-buffer.
    allocator.activate(commands.Get(),first);
    states.transition(commands.Get(),normal_texture.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands->ClearRenderTargetView(rtv,normal_clear,0,nullptr);
    states.transition(commands.Get(),normal_texture.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    REQUIRE(allocator.barrier_count()==3);
    REQUIRE(states.emitted_transitions()==6);
    REQUIRE(SUCCEEDED(commands->Close()));
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queue_desc{};queue_desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    REQUIRE(SUCCEEDED(device->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(queue.GetAddressOf()))));
    ID3D12CommandList* lists[]{commands.Get()};
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
    // WARP executed all alias barriers, clears and GPU fence successfully.
    // End the last aliasing ownership before releasing its parent heap.
    normal_texture.Reset();bloom_texture.Reset();
    allocator.reset();
    REQUIRE(!allocator.enabled());
    D3D12PlacedTransientAlias incompatible;
    bool overlap_rejected=false;
    try {
        northstarcpp::render::RenderGraph bad;
        auto x=bad.add_resource("A");
        auto y=bad.add_resource("B");
        bad.add_pass({"same pass",{}, {x,y},{}});
        incompatible.configure(device.Get(),bad,bad.compile(),x,normal,y,bloom);
    }catch(const std::logic_error&){overlap_rejected=true;}
    REQUIRE(overlap_rejected);
    // General N-resource allocation: four sequential resources can share
    // one physical slot, while an overlapping fifth occupies another heap.
    northstarcpp::render::RenderGraph multi;
    auto n0=multi.add_resource("n0");
    auto concurrent=multi.add_resource("overlap");
    auto n1=multi.add_resource("n1");
    auto n2=multi.add_resource("n2");
    auto n3=multi.add_resource("n3");
    auto q0=multi.add_pass({"q0",{}, {n0,concurrent},{}});
    auto q1=multi.add_pass({"q1",{concurrent},{n1},{q0}});
    auto q2=multi.add_pass({"q2",{concurrent},{n2},{q1}});
    multi.add_pass({"q3",{}, {n3},{q2}});
    const auto multi_plan=multi.compile();
    D3D12PlacedTransientAlias many;
    many.configure(device.Get(),multi,multi_plan,{
        {n0,normal},{concurrent,normal},{n1,normal},
        {n2,normal},{n3,normal}});
    REQUIRE(many.resource_count()==5);
    REQUIRE(many.heap_count()==1);
    REQUIRE(many.slot_count()==2);
    REQUIRE(many.physical_slot(n0)==many.physical_slot(n1));
    REQUIRE(many.physical_slot(n1)==many.physical_slot(n2));
    REQUIRE(many.physical_slot(n2)==many.physical_slot(n3));
    REQUIRE(many.physical_slot(concurrent)!=many.physical_slot(n0));
    REQUIRE(many.saved_bytes()>many.allocated_bytes());
    ComPtr<ID3D12Resource> active[5];
    const northstarcpp::render::ResourceId ids[5]{n0,concurrent,n1,n2,n3};
    for(int i=0;i<5;++i)
        many.create(device.Get(),ids[i],normal,clear,active[i]);
    ComPtr<ID3D12CommandAllocator> many_allocator;
    REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(many_allocator.GetAddressOf()))));
    ComPtr<ID3D12GraphicsCommandList> many_commands;
    REQUIRE(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,
        many_allocator.Get(),nullptr,IID_PPV_ARGS(many_commands.GetAddressOf()))));
    for(auto id:ids)many.activate(many_commands.Get(),id);
    REQUIRE(many.barrier_count()==5);
    REQUIRE(SUCCEEDED(many_commands->Close()));
    ID3D12CommandList* alias_lists[]{many_commands.Get()};
    queue->ExecuteCommandLists(1,alias_lists);
    REQUIRE(SUCCEEDED(queue->Signal(fence.Get(),2)));
    if(fence->GetCompletedValue()<2) {
        HANDLE sync=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        REQUIRE(sync!=nullptr);
        REQUIRE(SUCCEEDED(fence->SetEventOnCompletion(2,sync)));
        REQUIRE(WaitForSingleObject(sync,10000)==WAIT_OBJECT_0);
        CloseHandle(sync);
    }
    for(auto& resource:active)resource.Reset();
    many.reset();
    REQUIRE(!many.enabled());
    std::printf("WARP placed transient alias: 1 heap / 2 slots, 5 logical resources, "
                "5 alias barriers; overlapping lifetimes isolated\n");
}
