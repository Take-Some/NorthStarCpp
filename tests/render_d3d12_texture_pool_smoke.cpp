#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "../engine/northstarcpp/modules/render/d3d12/renderer_texture_pool.hpp"
#include "../engine/northstarcpp/modules/render/d3d12/renderer_resource_states.hpp"
#include <stdexcept>
#define REQUIRE(condition) do { if(!(condition)) \
    throw std::runtime_error("Runtime acceptance failed: " #condition); } while(false)
#include <cstdint>
#include <cstdio>
int main() {
    using Microsoft::WRL::ComPtr;
    using northstarcpp::render::d3d12::detail::D3D12TextureReusePool;
    ComPtr<IDXGIFactory4> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return 77;
    ComPtr<IDXGIAdapter> warp;
    if(FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))))return 77;
    ComPtr<ID3D12Device> device;
    if(FAILED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&device))))return 77;
    D3D12TextureReusePool pool;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=192;
    desc.Height=128;
    desc.DepthOrArraySize=1;
    desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format=desc.Format;
    ComPtr<ID3D12Resource> first,active,returned;
    pool.acquire(device.Get(),desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                 clear,first,"Pool test first texture");
    const auto* identity=first.Get();
    // A second simultaneous resource must not reuse the live allocation.
    pool.acquire(device.Get(),desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                 clear,active,"Pool test concurrent texture");
    REQUIRE(first.Get()!=active.Get());
    pool.recycle(device.Get(),first,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    REQUIRE(!first);
    REQUIRE(pool.count()==1);
    pool.acquire(device.Get(),desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                 clear,returned,"Pool test recycled texture");
    REQUIRE(returned.Get()==identity);
    REQUIRE(pool.cache_hits()==1);
    REQUIRE(pool.count()==0);
    // Incorrect optimized clear values must not use the saved allocation.
    pool.recycle(device.Get(),returned,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    D3D12_CLEAR_VALUE different_clear=clear;
    different_clear.Color[0]=.25f;
    ComPtr<ID3D12Resource> differently_cleared;
    pool.acquire(device.Get(),desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                 different_clear,differently_cleared,"Pool test different clear");
    REQUIRE(differently_cleared.Get()!=identity);
    REQUIRE(pool.cache_hits()==1);
    pool.recycle(device.Get(),differently_cleared,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // Matching size alone is insufficient: format and state must match.
    auto hdr_desc=desc;
    hdr_desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto hdr_clear=clear;hdr_clear.Format=hdr_desc.Format;
    ComPtr<ID3D12Resource> hdr;
    pool.acquire(device.Get(),hdr_desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                 hdr_clear,hdr,"Pool test incompatible format");
    REQUIRE(hdr.Get()!=identity);
    pool.recycle(device.Get(),hdr,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    pool.recycle(device.Get(),active,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    REQUIRE(pool.cached_bytes()<=D3D12TextureReusePool::budget_bytes);
    REQUIRE(pool.count()>=2);
    // All release-mode checks below must remain live with NDEBUG defined.
    using northstarcpp::render::d3d12::detail::D3D12ResourceStates;
    ComPtr<ID3D12Resource> tracked;
    pool.acquire(device.Get(),desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                 clear,tracked,"State tracker acceptance texture");
    D3D12ResourceStates states;
    states.seed(tracked.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    REQUIRE(states.contains(tracked.Get()));
    REQUIRE(states.state(tracked.Get())==D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    bool rejected=false;
    try{states.seed(tracked.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);}
    catch(const std::logic_error&){rejected=true;}
    REQUIRE(rejected);
    ComPtr<ID3D12CommandAllocator> allocator;
    REQUIRE(SUCCEEDED(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
    ComPtr<ID3D12GraphicsCommandList> commands;
    REQUIRE(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator.Get(),nullptr,IID_PPV_ARGS(&commands))));
    states.transition(commands.Get(),tracked.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
    REQUIRE(states.emitted_transitions()==1);
    states.transition(commands.Get(),tracked.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
    REQUIRE(states.emitted_transitions()==1); // no redundant barriers
    states.transition(commands.Get(),tracked.Get(),
                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    REQUIRE(states.state(tracked.Get())==D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    REQUIRE(states.emitted_transitions()==2);
    rejected=false;
    try{states.transition(commands.Get(),nullptr,D3D12_RESOURCE_STATE_RENDER_TARGET);}
    catch(const std::logic_error&){rejected=true;}
    REQUIRE(rejected);
    REQUIRE(SUCCEEDED(commands->Close()));
    pool.recycle(device.Get(),tracked,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    std::printf("D3D12 WARP target reuse: hits=%llu cached=%llu bytes\n",
        static_cast<unsigned long long>(pool.cache_hits()),
        static_cast<unsigned long long>(pool.cached_bytes()));
}
