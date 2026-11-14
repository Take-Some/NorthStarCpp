// NorthStarCpp - native D3D12 provider, stable engine.render ABI 5.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/render_plugin.h>
#include <ui_quad.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/geometry.hpp>
#include <northstarcpp/core/math_standalone.h>
#include <northstarcpp/scene.hpp>
#include "module_provider.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
using namespace northstarcpp::module_provider;
using Microsoft::WRL::ComPtr;
void check(HRESULT hr,const char* name) {
    if(FAILED(hr)) {char error[160];
        std::snprintf(error,sizeof(error),"%s: 0x%08lx",name,static_cast<unsigned long>(hr));
        throw std::runtime_error(error);}
}
struct Vertex {float x,y,z,r,g,b;};
constexpr UINT capacity=400000,buffer_count=2;
D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h{};h.Type=type;h.CreationNodeMask=h.VisibleNodeMask=1;return h;
}
D3D12_RESOURCE_DESC memory(UINT64 bytes) {
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width=bytes;d.Height=d.DepthOrArraySize=d.MipLevels=1;
    d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return d;
}
ComPtr<ID3DBlob> compile(const char* entry) {
    static const char code[]=R"(
cbuffer Camera : register(b0) {row_major float4x4 vp;};
struct V {float3 pos:POSITION;float3 rgb:COLOR;};
struct P {float4 pos:SV_Position;float3 rgb:COLOR;};
P vs(V v){P p;p.pos=mul(float4(v.pos,1),vp);p.rgb=v.rgb;return p;}
float4 ps(P p):SV_Target{return float4(p.rgb,1);}
)";
    ComPtr<ID3DBlob> binary,errors;
    auto hr=D3DCompile(code,sizeof(code)-1,"NorthStar D3D12",nullptr,nullptr,
        entry,entry[0]=='v'?"vs_5_0":"ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,0,&binary,&errors);
    if(FAILED(hr)&&errors)
        throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    check(hr,"Compile D3D12 shader");return binary;
}
constexpr char sky_shader_code[] = R"(
cbuffer Camera : register(b0) { row_major float4x4 view_projection; };
cbuffer SkyTint : register(b1) { float4 sky_tint; };
Texture2D sky_texture : register(t0);
SamplerState sky_sampler : register(s0);
struct SkyVertex { float3 position : POSITION; float2 uv : TEXCOORD0; };
struct SkyPixel { float4 position : SV_Position; float3 direction : TEXCOORD0; };
SkyPixel sky_vertex_main(SkyVertex v) {
    SkyPixel p;
    p.position = mul(float4(v.position * 100.0, 1.0), view_projection);
    // Sky UVs are not a photographic cloud map; the Perlin map is volumetric noise.
    p.direction = v.position;
    return p;
}
float sky_noise(float3 dir, float frequency) {
    // Triplanar sampling avoids seam and pole artifacts.
    float3 weights = pow(abs(dir), 4.0);
    weights /= max(dot(weights, float3(1.0, 1.0, 1.0)), 0.0001);
    float3 yz = sky_texture.Sample(sky_sampler, dir.yz * frequency).rgb;
    float3 xz = sky_texture.Sample(sky_sampler, dir.xz * frequency).rgb;
    float3 xy = sky_texture.Sample(sky_sampler, dir.xy * frequency).rgb;
    float3 n = float3(dot(yz, float3(0.33333, 0.33333, 0.33334)),
                      dot(xz, float3(0.33333, 0.33333, 0.33334)),
                      dot(xy, float3(0.33333, 0.33333, 0.33334)));
    return dot(n, weights);
}
float4 sky_pixel_main(SkyPixel p) : SV_Target {
    float3 direction = normalize(p.direction);
    float altitude = saturate(direction.y * 0.5 + 0.5);
    float zenith = smoothstep(0.08, 0.97, altitude);
    float3 color = lerp(float3(0.46, 0.65, 0.88),
                        float3(0.075, 0.22, 0.47), zenith);
    // The authored base_color grades the atmosphere, not the noise RGB.
    color *= lerp(float3(1.0, 1.0, 1.0), sky_tint.rgb * 1.6, 0.18);
    float density = sky_noise(direction, 2.1) * 0.78 +
                    sky_noise(direction, 6.7) * 0.22;
    float clouds = smoothstep(0.54, 0.75, density);
    float visibility = smoothstep(0.02, 0.27, altitude);
    float3 cloud_color = lerp(float3(0.68, 0.78, 0.89),
                              float3(0.92, 0.94, 0.97), altitude);
    color = lerp(color, cloud_color, clouds * visibility * 0.67);
    return float4(saturate(color), 1.0);
}
)";

ComPtr<ID3DBlob> compile_sky(const char* entry,const char* profile) {
    ComPtr<ID3DBlob> compiled,errors;
    HRESULT result=D3DCompile(sky_shader_code,sizeof(sky_shader_code)-1,
        "NorthStar D3D12 sky",nullptr,nullptr,entry,profile,
        D3DCOMPILE_ENABLE_STRICTNESS,0,&compiled,&errors);
    if(FAILED(result)&&errors)
        throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    check(result,"Compile D3D12 sky");
    return compiled;
}
struct SkyVertex {float x,y,z,u,v;};
static_assert(sizeof(SkyVertex)==sizeof(NorthStarSkyVertex));
void make_hud(NorthStarRenderFrame const& frame,UINT width,UINT height,
              std::vector<Vertex>& hud_triangles,std::vector<Vertex>& hud_lines) {
        float const hx = 2.f / static_cast<float>(width);
        float const hy = 2.f / static_cast<float>(height);
        hud_lines.reserve(12);
        hud_triangles.reserve(270);
        auto rect = [&](float x0, float y0, float x1, float y1,
                        float r, float g, float b) {
            auto const corners = northstarcpp::ui::quad_corners(x0, y0, x1, y1);
            for (auto index : northstarcpp::ui::quad_triangle_corners) {
                auto const& corner = corners[index];
                hud_triangles.push_back({corner.x, corner.y, 0.f, r, g, b});
            }
        };
        auto segment = [&](float x0, float y0, float x1, float y1) {
            constexpr float r = 0.38f, g = 0.94f, b = 0.88f;
            hud_lines.push_back({x0, y0, 0.f, r, g, b});
            hud_lines.push_back({x1, y1, 0.f, r, g, b});
        };
        // Center reticle with a clear space around the point of aim.
        segment(-16 * hx, 0.f, -5 * hx, 0.f);
        segment(5 * hx, 0.f, 16 * hx, 0.f);
        segment(0.f, -16 * hy, 0.f, -5 * hy);
        segment(0.f, 5 * hy, 0.f, 16 * hy);
        // Health bar and ammo marks; colors are GPU-side, no GDI over swap chain.
        rect(-0.94f,0.83f,-0.36f,0.89f,0.07f,0.13f,0.18f);
        float const health = frame.player_max_health ?
            std::clamp(float(frame.player_health)/frame.player_max_health,0.f,1.f) : 0.f;
        rect(-0.93f,0.84f,-0.93f+0.56f*health,0.88f,
             health < 0.26f ? 0.95f : 0.14f,
             health < 0.26f ? 0.22f : 0.87f,
             health < 0.26f ? 0.21f : 0.58f);
        rect(0.30f,-0.89f,0.94f,-0.82f,0.07f,0.13f,0.18f);
        auto const capacity = std::min(frame.magazine_capacity, 32u);
        for (uint32_t i=0; i<capacity; ++i) {
            float const start = 0.315f + i*(0.60f/static_cast<float>(capacity));
            bool const filled = i<frame.ammunition;
            rect(start,-0.878f,start+0.60f/capacity*0.7f,-0.832f,
                 filled ? 0.96f : 0.18f, filled ? 0.71f : 0.24f, filled ? 0.36f : 0.29f);
        }
        // The initial weapon is a stylized low-poly HUD model, not a 3D rig.
        // It visibly lifts on each shot and returns with the recoil spring.
        float const kick = std::clamp(frame.weapon_recoil,0.f,1.6f)*0.075f;
        rect(0.38f,-1.f+kick,0.76f,-0.59f+kick,0.09f,0.17f,0.22f);
        rect(0.40f,-0.72f+kick,0.79f,-0.64f+kick,0.17f,0.31f,0.36f);
        rect(0.49f,-0.65f+kick,0.71f,-0.49f+kick,0.23f,0.41f,0.43f);
        rect(0.54f,-0.51f+kick,0.66f,-0.39f+kick,0.11f,0.23f,0.27f);
        rect(0.55f,-0.40f+kick,0.65f,-0.37f+kick,0.29f,0.69f,0.68f);
        // Muted menu backdrop allows native Win32 buttons to remain accessible.
        if (frame.overlay_paused)
            rect(-0.56f,-0.56f,0.56f,0.56f,0.035f,0.075f,0.105f);
}

void cube(std::vector<Vertex>& vertices,const NorthStarRenderCube& c,bool selected) {
    auto const mesh=northstarcpp::geometry::primitive(northstarcpp::geometry::SolidPrimitive::cube);
    constexpr float shade[]={.74f,1.f,.60f,.82f,.95f,.68f};
    auto rgb=selected?0xffd474u:c.rgb;
    float r=float((rgb>>16)&255)/255.f,g=float((rgb>>8)&255)/255.f,b=float(rgb&255)/255.f;
    float h=c.size*.5f;
    for(size_t t=0;t<mesh.index_count;t+=3) {
        auto a=mesh.positions+3*mesh.indices[t],p=mesh.positions+3*mesh.indices[t+1],
            q=mesh.positions+3*mesh.indices[t+2];
        unsigned face=a[1]==p[1]&&a[1]==q[1]?(a[1]>0?1:0):
            a[2]==p[2]&&a[2]==q[2]?(a[2]>0?4:2):(a[0]>0?3:5);
        for(int j=0;j<3;++j) {
            auto v=mesh.positions+3*mesh.indices[t+j];
            float x=v[0]*h,y=v[1]*h,z=v[2]*h;
            vertices.push_back({c.x+c.rotation[0]*x+c.rotation[1]*y+c.rotation[2]*z,
                c.y+h+c.rotation[3]*x+c.rotation[4]*y+c.rotation[5]*z,
                c.z+c.rotation[6]*x+c.rotation[7]*y+c.rotation[8]*z,
                r*shade[face],g*shade[face],b*shade[face]});
        }
    }
}
struct Renderer {
    HWND hwnd;
    UINT width=0,height=0,stride=0;
    UINT64 fence_value=0;
    HANDLE event=nullptr;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapchain;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12DescriptorHeap> rtvs,dsvs;
    ComPtr<ID3D12Resource> backs[buffer_count],depth,vertex_buffer,constant_buffer;
    ComPtr<ID3D12RootSignature> signature;
    ComPtr<ID3D12PipelineState> triangles,lines;
    D3D12_VERTEX_BUFFER_VIEW vertex_view{};
    unsigned char* vertex_data=nullptr;
    unsigned char* camera_data=nullptr;
    std::vector<Vertex> mesh_vertices;
    ComPtr<ID3D12RootSignature> sky_signature;
    ComPtr<ID3D12PipelineState> sky_pipeline,hud_triangles_pso,hud_lines_pso;
    ComPtr<ID3D12DescriptorHeap> sky_srv_heap;
    ComPtr<ID3D12Resource> sky_vertex,sky_index,sky_texture;
    ComPtr<ID3D12Resource> sky_camera,sky_tint,hud_camera;
    D3D12_VERTEX_BUFFER_VIEW sky_vertex_view{};
    D3D12_INDEX_BUFFER_VIEW sky_index_view{};
    unsigned char* sky_camera_data=nullptr;
    unsigned char* sky_tint_data=nullptr;
    unsigned char* hud_camera_data=nullptr;
    UINT sky_index_count=0;
    UINT64 sky_gpu_bytes=0;
    Renderer(HWND window,bool software):hwnd(window) {
        if(!IsWindow(hwnd)) throw std::runtime_error("Invalid D3D12 HWND");
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create DXGI");
        if(software) {
            ComPtr<IDXGIAdapter> warp;
            check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"Get WARP adapter");
            check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&device)),"Create WARP D3D12");
        } else {
            for(UINT i=0;;++i) {
                ComPtr<IDXGIAdapter1> adapter;
                if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 info{};adapter->GetDesc1(&info);
                if(info.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
                if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&device))))break;
            }
            if(!device)throw std::runtime_error("D3D12 hardware unavailable");
        }
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue)),"Create queue");
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)),"Create allocator");
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"Create commands");
        check(commands->Close(),"Close initial commands");
        check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!event)throw std::runtime_error("Create D3D12 fence event");
        D3D12_DESCRIPTOR_HEAP_DESC dh{};
        dh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;dh.NumDescriptors=buffer_count;
        check(device->CreateDescriptorHeap(&dh,IID_PPV_ARGS(&rtvs)),"Create RTV heap");
        stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        dh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;dh.NumDescriptors=1;
        check(device->CreateDescriptorHeap(&dh,IID_PPV_ARGS(&dsvs)),"Create DSV heap");
        D3D12_ROOT_PARAMETER root_param{};
        root_param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_param.Descriptor.ShaderRegister=0;
        root_param.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC root{};
        root.NumParameters=1;root.pParameters=&root_param;
        root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> binary,error;
        check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,
            &binary,&error),"Serialize root signature");
        check(device->CreateRootSignature(0,binary->GetBufferPointer(),binary->GetBufferSize(),
            IID_PPV_ARGS(&signature)),"Create root signature");
        auto vs=compile("vs"),ps=compile("ps");
        D3D12_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(Vertex,x),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(Vertex,r),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature=signature.Get();
        pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
        pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        pso.InputLayout={elements,2};pso.SampleMask=UINT_MAX;pso.SampleDesc.Count=1;
        pso.NumRenderTargets=1;pso.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
        pso.DSVFormat=DXGI_FORMAT_D32_FLOAT;
        pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable=TRUE;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.DepthStencilState.DepthEnable=TRUE;
        pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
        pso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
        pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&triangles)),
            "Create D3D12 triangles PSO");
        pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&lines)),
            "Create D3D12 lines PSO");
        pso.DepthStencilState.DepthEnable=FALSE;
        pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&hud_triangles_pso)),
            "Create HUD triangle PSO");
        pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&hud_lines_pso)),
            "Create HUD line PSO");
        // The skydome root uses b0 for its camera, b1 for material tint,
        // and a shader-visible SRV table for the authored RGBA8 YTD texture.
        D3D12_ROOT_PARAMETER sky_params[3]{};
        sky_params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        sky_params[0].Descriptor.ShaderRegister=0;
        sky_params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        sky_params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        sky_params[1].Descriptor.ShaderRegister=1;
        sky_params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors=1;range.BaseShaderRegister=0;
        sky_params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        sky_params[2].DescriptorTable.NumDescriptorRanges=1;
        sky_params[2].DescriptorTable.pDescriptorRanges=&range;
        sky_params[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxLOD=D3D12_FLOAT32_MAX;
        sampler.MaxAnisotropy=1;
        sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC sky_root{};
        sky_root.NumParameters=3;sky_root.pParameters=sky_params;
        sky_root.NumStaticSamplers=1;sky_root.pStaticSamplers=&sampler;
        sky_root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        binary.Reset();error.Reset();
        check(D3D12SerializeRootSignature(&sky_root,D3D_ROOT_SIGNATURE_VERSION_1,
            &binary,&error),"Serialize skydome root signature");
        check(device->CreateRootSignature(0,binary->GetBufferPointer(),binary->GetBufferSize(),
            IID_PPV_ARGS(&sky_signature)),"Create skydome root signature");
        auto sky_vs=compile_sky("sky_vertex_main","vs_5_0");
        auto sky_ps=compile_sky("sky_pixel_main","ps_5_0");
        D3D12_INPUT_ELEMENT_DESC sky_elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(SkyVertex,x),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(SkyVertex,u),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC sky_pso=pso;
        sky_pso.pRootSignature=sky_signature.Get();
        sky_pso.VS={sky_vs->GetBufferPointer(),sky_vs->GetBufferSize()};
        sky_pso.PS={sky_ps->GetBufferPointer(),sky_ps->GetBufferSize()};
        sky_pso.InputLayout={sky_elements,2};
        sky_pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        check(device->CreateGraphicsPipelineState(&sky_pso,IID_PPV_ARGS(&sky_pipeline)),
            "Create native D3D12 skydome pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
        srv_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_desc.NumDescriptors=1;srv_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&srv_desc,IID_PPV_ARGS(&sky_srv_heap)),
            "Create skydome SRV heap");
        auto upload=heap(D3D12_HEAP_TYPE_UPLOAD);
        auto vertices=memory(UINT64(capacity)*sizeof(Vertex));
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&vertices,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&vertex_buffer)),
            "Create upload vertices");
        auto constants=memory(256);
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&constants,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&constant_buffer)),
            "Create camera constants");
        check(vertex_buffer->Map(0,nullptr,reinterpret_cast<void**>(&vertex_data)),
            "Map vertex upload");
        check(constant_buffer->Map(0,nullptr,reinterpret_cast<void**>(&camera_data)),
            "Map camera upload");
        auto make_constant=[&](ComPtr<ID3D12Resource>& buffer,unsigned char** bytes) {
            check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&constants,
                D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&buffer)),
                "Create D3D12 render constants");
            check(buffer->Map(0,nullptr,reinterpret_cast<void**>(bytes)),
                "Map D3D12 render constants");
        };
        make_constant(sky_camera,&sky_camera_data);
        make_constant(sky_tint,&sky_tint_data);
        make_constant(hud_camera,&hud_camera_data);
        using namespace northstarcpp::math;
        float4x4 identity{float4(1.f,0.f,0.f,0.f),float4(0.f,1.f,0.f,0.f),
                          float4(0.f,0.f,1.f,0.f),float4(0.f,0.f,0.f,1.f)};
        std::memcpy(hud_camera_data,&identity,sizeof(identity));
        vertex_view={vertex_buffer->GetGPUVirtualAddress(),capacity*sizeof(Vertex),sizeof(Vertex)};
        DXGI_SWAP_CHAIN_DESC1 chain{};
        chain.Format=DXGI_FORMAT_R8G8B8A8_UNORM;chain.SampleDesc.Count=1;
        chain.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;chain.BufferCount=buffer_count;
        chain.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> created;
        check(factory->CreateSwapChainForHwnd(queue.Get(),hwnd,&chain,nullptr,nullptr,
            &created),"Create D3D12 swapchain");
        check(created.As(&swapchain),"Query D3D12 swapchain");
        factory->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER);
        RECT rect{};GetClientRect(hwnd,&rect);
        if(rect.right>0&&rect.bottom>0)resize(rect.right,rect.bottom);
    }
    ~Renderer() {
        if(queue&&fence) {try{wait();}catch(...){}}
        if(vertex_buffer&&vertex_data)vertex_buffer->Unmap(0,nullptr);
        if(constant_buffer&&camera_data)constant_buffer->Unmap(0,nullptr);
        if(sky_camera&&sky_camera_data)sky_camera->Unmap(0,nullptr);
        if(sky_tint&&sky_tint_data)sky_tint->Unmap(0,nullptr);
        if(hud_camera&&hud_camera_data)hud_camera->Unmap(0,nullptr);
        if(event)CloseHandle(event);
    }
    void wait() {
        UINT64 next=++fence_value;
        check(queue->Signal(fence.Get(),next),"Signal D3D12 fence");
        if(fence->GetCompletedValue()<next) {
            check(fence->SetEventOnCompletion(next,event),"Wait D3D12 fence");
            if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)
                throw std::runtime_error("D3D12 GPU timeout");
        }
    }
    void resize(UINT w,UINT h) {
        if(w==width&&h==height)return;
        wait();
        for(auto& b:backs)b.Reset();depth.Reset();
        check(swapchain->ResizeBuffers(buffer_count,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,0),
            "Resize D3D12 swapchain");
        auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<buffer_count;++i) {
            check(swapchain->GetBuffer(i,IID_PPV_ARGS(&backs[i])),"Get D3D12 buffer");
            device->CreateRenderTargetView(backs[i].Get(),nullptr,rtv);
            rtv.ptr+=stride;
        }
        D3D12_RESOURCE_DESC d{};
        d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
        d.DepthOrArraySize=1;d.MipLevels=1;d.Format=DXGI_FORMAT_D32_FLOAT;
        d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};clear.Format=d.Format;clear.DepthStencil.Depth=1.f;
        auto memory_heap=heap(D3D12_HEAP_TYPE_DEFAULT);
        check(device->CreateCommittedResource(&memory_heap,D3D12_HEAP_FLAG_NONE,&d,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth)),
            "Create D3D12 depth");
        device->CreateDepthStencilView(depth.Get(),nullptr,
            dsvs->GetCPUDescriptorHandleForHeapStart());
        width=w;height=h;
    }
    void set_triangles(const NorthStarRenderTriangleVertex* source,UINT count) {
        if(count>300000||count%3||(count&&!source))
            throw std::invalid_argument("Invalid D3D12 mesh triangle count");
        std::vector<Vertex> result;result.reserve(count);
        for(UINT i=0;i<count;++i) {
            auto const& v=source[i];
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
                throw std::invalid_argument("Invalid D3D12 mesh vertex");
            result.push_back({v.x,v.y,v.z,float((v.rgb>>16)&255)/255.f,
                float((v.rgb>>8)&255)/255.f,float(v.rgb&255)/255.f});
        }
        mesh_vertices.swap(result);
    }
    // Own all geometry and texture contents after return; the host may free its
    // YDD/YTD/ymat buffers immediately.
    void set_sky(NorthStarSky const* source) {
        wait();
        if(!source) {
            sky_index_count=0;sky_gpu_bytes=0;
            sky_vertex.Reset();sky_index.Reset();sky_texture.Reset();
            return;
        }
        if(!source->vertices||!source->indices||!source->rgba8||
           source->vertex_count<3||source->vertex_count>65536||
           source->index_count<3||source->index_count>1000000||
           source->index_count%3||!source->width||!source->height||
           source->width>8192||source->height>8192)
            throw std::invalid_argument("Invalid D3D12 skydome resource dimensions");
        for(UINT i=0;i<source->index_count;++i)
            if(source->indices[i]>=source->vertex_count)
                throw std::invalid_argument("D3D12 skydome index out of bounds");
        for(UINT i=0;i<source->vertex_count;++i) {
            auto const& v=source->vertices[i];
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||
               !std::isfinite(v.u)||!std::isfinite(v.v))
                throw std::invalid_argument("Non-finite D3D12 skydome vertex");
        }
        float tint[4]={source->tint_r,source->tint_g,source->tint_b,1.f};
        for(float c:tint)
            if(!std::isfinite(c)||c<0.f||c>4.f)
                throw std::invalid_argument("Invalid skydome material tint");
        auto upload=heap(D3D12_HEAP_TYPE_UPLOAD);
        auto upload_buffer=[&](const void* data,UINT64 bytes) {
            ComPtr<ID3D12Resource> resource;
            auto d=memory(bytes);
            check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&d,
                D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource)),
                "Create D3D12 skydome geometry buffer");
            void* mapped=nullptr;
            check(resource->Map(0,nullptr,&mapped),"Map skydome geometry");
            std::memcpy(mapped,data,size_t(bytes));
            resource->Unmap(0,nullptr);
            return resource;
        };
        UINT64 vb_bytes=UINT64(source->vertex_count)*sizeof(SkyVertex);
        UINT64 ib_bytes=UINT64(source->index_count)*sizeof(uint32_t);
        auto new_vertex=upload_buffer(source->vertices,vb_bytes);
        auto new_index=upload_buffer(source->indices,ib_bytes);
        UINT pitch=(source->width*4u+255u)&~255u;
        auto staging_desc=memory(UINT64(pitch)*source->height);
        ComPtr<ID3D12Resource> staging;
        check(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,
            &staging_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
            IID_PPV_ARGS(&staging)),"Allocate skydome texture staging");
        unsigned char* mapped=nullptr;
        check(staging->Map(0,nullptr,reinterpret_cast<void**>(&mapped)),
              "Map skydome texture staging");
        for(UINT y=0;y<source->height;++y)
            std::memcpy(mapped+size_t(y)*pitch,
                source->rgba8+size_t(y)*source->width*4u,source->width*4u);
        staging->Unmap(0,nullptr);
        D3D12_RESOURCE_DESC tex{};
        tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex.Width=source->width;tex.Height=source->height;
        tex.DepthOrArraySize=1;tex.MipLevels=1;
        tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;tex.SampleDesc.Count=1;
        auto default_heap=heap(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> new_texture;
        check(device->CreateCommittedResource(&default_heap,D3D12_HEAP_FLAG_NONE,
            &tex,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
            IID_PPV_ARGS(&new_texture)),"Allocate D3D12 skydome texture");
        check(allocator->Reset(),"Reset skydome upload allocator");
        check(commands->Reset(allocator.Get(),nullptr),"Reset skydome upload list");
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource=new_texture.Get();
        dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource=staging.Get();
        src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format=tex.Format;
        src.PlacedFootprint.Footprint.Width=source->width;
        src.PlacedFootprint.Footprint.Height=source->height;
        src.PlacedFootprint.Footprint.Depth=1;
        src.PlacedFootprint.Footprint.RowPitch=pitch;
        commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=new_texture.Get();
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commands->ResourceBarrier(1,&barrier);
        check(commands->Close(),"Close skydome upload");
        ID3D12CommandList* lists[]={commands.Get()};
        queue->ExecuteCommandLists(1,lists);
        wait();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format=tex.Format;
        srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels=1;
        device->CreateShaderResourceView(new_texture.Get(),&srv,
            sky_srv_heap->GetCPUDescriptorHandleForHeapStart());
        sky_vertex=std::move(new_vertex);
        sky_index=std::move(new_index);
        sky_texture=std::move(new_texture);
        sky_vertex_view={sky_vertex->GetGPUVirtualAddress(),UINT(vb_bytes),sizeof(SkyVertex)};
        sky_index_view={sky_index->GetGPUVirtualAddress(),UINT(ib_bytes),DXGI_FORMAT_R32_UINT};
        std::memcpy(sky_tint_data,tint,sizeof(tint));
        sky_index_count=source->index_count;
        sky_gpu_bytes=vb_bytes+ib_bytes+UINT64(source->width)*source->height*4u;
    }

    NorthStarRenderMemoryStats memory_stats() const {
        NorthStarRenderMemoryStats stats{};
        stats.vertex_buffer_bytes=UINT64(capacity)*sizeof(Vertex);
        stats.world_vertex_capacity=capacity;
        stats.sky_geometry_and_texture_bytes=sky_gpu_bytes;
        return stats;
    }

    // Genuine DXGI back-buffer inspection. A fence alone is not pixel verification.
    void verify_readback(ID3D12Resource* pixels,UINT pitch) {
        void* data=nullptr;
        D3D12_RANGE range{0,SIZE_T(pitch)*height};
        check(pixels->Map(0,&range,&data),"Map D3D12 GPU readback");
        UINT64 colored=0,sky_samples=0,sky_pixels=0;
        auto const* bytes=static_cast<const uint8_t*>(data);
        for(UINT y=0;y<height;++y) {
            auto const* row=bytes+SIZE_T(y)*pitch;
            for(UINT x=0;x<width;++x) {
                auto const* pixel=row+4u*x;
                if(pixel[0]>110||pixel[1]>110||pixel[2]>110)++colored;
                if(sky_index_count&&y<height/5&&x>width/3&&x<width*2/3) {
                    ++sky_samples;
                    if(pixel[0]>62||pixel[1]>62||pixel[2]>62)++sky_pixels;
                }
            }
        }
        D3D12_RANGE no_writes{0,0};
        pixels->Unmap(0,&no_writes);
        if(colored<100)throw std::runtime_error(
            "D3D12 back-buffer readback contains no visible scene geometry");
        if(sky_index_count&&(!sky_samples||sky_pixels*10<sky_samples))
            throw std::runtime_error("D3D12 skydome readback lacks sky coverage");
    }

    void draw(const NorthStarRenderFrame& frame) {
        if(frame.count>northstarcpp::max_scene_objects||(frame.count&&!frame.cubes))
            throw std::invalid_argument("Invalid render frame");
        RECT rect{};GetClientRect(hwnd,&rect);
        if(rect.right<=0||rect.bottom<=0)return;
        resize(rect.right,rect.bottom);
        using namespace northstarcpp::math;
        float3 focus(frame.camera_x,frame.camera_y,frame.camera_z);
        float3 direction(std::sin(frame.camera_yaw)*std::cos(frame.camera_pitch),
            std::sin(frame.camera_pitch),std::cos(frame.camera_yaw)*std::cos(frame.camera_pitch));
        float3 right(std::cos(frame.camera_yaw),0.f,-std::sin(frame.camera_yaw));
        float3 up=frame.camera_override
            ?(direction^right)*std::cos(frame.camera_roll)+right*std::sin(frame.camera_roll)
            :float3(0.f,1.f,0.f);
        auto view=frame.camera_override||frame.first_person
            ?create_camera_at(focus,focus+direction,up)
            :create_camera_at(focus+float3(8.f,7.f,-10.f),focus,float3(0.f,1.f,0.f));
        float fov=frame.camera_override?frame.camera_fov_y_degrees*.01745329252f:pi_d4;
        if(!std::isfinite(fov)||fov<=0.f||fov>3.f)
            throw std::invalid_argument("Invalid camera field of view");
        auto projection=create_projection(fov,float(height)/width,
            frame.camera_override?frame.camera_near_clip:.1f,
            frame.camera_override?frame.camera_far_clip:250.f);
        auto matrix=mul4x4(view,projection);
        std::vector<Vertex> vertices;
        vertices.reserve(std::min<size_t>(capacity,frame.count*36u+84u+mesh_vertices.size()));
        if(!frame.first_person)for(int n=-10;n<=10;++n) {
            float k=float(n),tone=n==0?.38f:.23f;
            vertices.push_back({k,-.01f,-10.f,tone,tone,tone});
            vertices.push_back({k,-.01f,10.f,tone,tone,tone});
            vertices.push_back({-10.f,-.01f,k,tone,tone,tone});
            vertices.push_back({10.f,-.01f,k,tone,tone,tone});
        }
        UINT grid=UINT(vertices.size());
        frustum visible(matrix,clip_depth::zero_to_one);
        for(UINT i=0;i<frame.count;++i) {
            auto const& c=frame.cubes[i];
            if(!std::isfinite(c.size)||c.size<=0.f)continue;
            float half=c.size*.5f;
            float4x4 tr;
            tr.i=float4(c.rotation[0]*half,c.rotation[1]*half,c.rotation[2]*half,0.f);
            tr.j=float4(c.rotation[3]*half,c.rotation[4]*half,c.rotation[5]*half,0.f);
            tr.k=float4(c.rotation[6]*half,c.rotation[7]*half,c.rotation[8]*half,0.f);
            tr.c=float4(c.x,c.y+half,c.z,1.f);
            aabb bounds;bounds.min={-1.f,-1.f,-1.f};bounds.max={1.f,1.f,1.f};
            bounds.modify(tr);bounds+=.05f;
            if(visible.test_inexact(bounds)==intersection_outside)continue;
            if(vertices.size()+36u+mesh_vertices.size()>capacity)
                throw std::runtime_error("D3D12 transient vertex budget exceeded");
            cube(vertices,c,i==UINT(frame.selected));
        }
        if(vertices.size()+mesh_vertices.size()>capacity)
            throw std::runtime_error("D3D12 triangle budget exceeded");
        vertices.insert(vertices.end(),mesh_vertices.begin(),mesh_vertices.end());
        // HUD vertices use native clip coordinates and a separate identity matrix.
        UINT hud_tri_start=UINT(vertices.size());
        std::vector<Vertex> hud_tri,hud_line;
        if(frame.first_person&&frame.player_max_health)
            make_hud(frame,width,height,hud_tri,hud_line);
        if(vertices.size()+hud_tri.size()+hud_line.size()>capacity)
            throw std::runtime_error("D3D12 HUD vertex budget exceeded");
        vertices.insert(vertices.end(),hud_tri.begin(),hud_tri.end());
        UINT hud_line_start=UINT(vertices.size());
        vertices.insert(vertices.end(),hud_line.begin(),hud_line.end());
        // A deliberately conservative single frame in flight, safe for resizing
        // and transient CPU-side uploads; optimize once profiling is available.
        wait();
        if(!vertices.empty())
            std::memcpy(vertex_data,vertices.data(),vertices.size()*sizeof(Vertex));
        std::memcpy(camera_data,&matrix,sizeof(matrix));
        if(sky_index_count) {
            auto sky_matrix=mul4x4(create_camera_at(float3(0.f,0.f,0.f),
                direction,up),projection);
            std::memcpy(sky_camera_data,&sky_matrix,sizeof(sky_matrix));
        }
        UINT index=swapchain->GetCurrentBackBufferIndex();
        check(allocator->Reset(),"Reset allocator");
        check(commands->Reset(allocator.Get(),nullptr),"Reset command list");
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=backs[index].Get();
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
        commands->ResourceBarrier(1,&barrier);
        auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=index*stride;
        auto dsv=dsvs->GetCPUDescriptorHandleForHeapStart();
        commands->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
        const float clear[]={.055f,.075f,.105f,1.f};
        commands->ClearRenderTargetView(rtv,clear,0,nullptr);
        commands->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1.f,0,0,nullptr);
        D3D12_VIEWPORT viewport{0.f,0.f,float(width),float(height),0.f,1.f};
        D3D12_RECT scissors{0,0,LONG(width),LONG(height)};
        commands->RSSetViewports(1,&viewport);
        commands->RSSetScissorRects(1,&scissors);
        // Draw authored skydome before the opaque world. Its PSO disables
        // depth testing/writes, matching the existing D3D11 world order.
        if(sky_index_count) {
            ID3D12DescriptorHeap* heaps[]={sky_srv_heap.Get()};
            commands->SetDescriptorHeaps(1,heaps);
            commands->SetGraphicsRootSignature(sky_signature.Get());
            commands->SetGraphicsRootConstantBufferView(
                0,sky_camera->GetGPUVirtualAddress());
            commands->SetGraphicsRootConstantBufferView(
                1,sky_tint->GetGPUVirtualAddress());
            commands->SetGraphicsRootDescriptorTable(
                2,sky_srv_heap->GetGPUDescriptorHandleForHeapStart());
            commands->SetPipelineState(sky_pipeline.Get());
            commands->IASetVertexBuffers(0,1,&sky_vertex_view);
            commands->IASetIndexBuffer(&sky_index_view);
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commands->DrawIndexedInstanced(sky_index_count,1,0,0,0);
        }
        commands->SetGraphicsRootSignature(signature.Get());
        commands->SetGraphicsRootConstantBufferView(0,constant_buffer->GetGPUVirtualAddress());
        commands->IASetVertexBuffers(0,1,&vertex_view);
        if(grid) {
            commands->SetPipelineState(lines.Get());
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            commands->DrawInstanced(grid,1,0,0);
        }
        if(hud_tri_start>grid) {
            commands->SetPipelineState(triangles.Get());
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commands->DrawInstanced(hud_tri_start-grid,1,grid,0);
        }
        if(hud_tri_start<hud_line_start||hud_line_start<vertices.size()) {
            commands->SetGraphicsRootConstantBufferView(
                0,hud_camera->GetGPUVirtualAddress());
            if(hud_line_start>hud_tri_start) {
                commands->SetPipelineState(hud_triangles_pso.Get());
                commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                commands->DrawInstanced(hud_line_start-hud_tri_start,1,hud_tri_start,0);
            }
            if(vertices.size()>hud_line_start) {
                commands->SetPipelineState(hud_lines_pso.Get());
                commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
                commands->DrawInstanced(UINT(vertices.size())-hud_line_start,1,hud_line_start,0);
            }
        }
        // Copy the actual rendered back buffer into a row-aligned READBACK
        // heap before transitioning it back to PRESENT.
        ComPtr<ID3D12Resource> pixels;
        UINT pitch=0;
        if(frame.verify_pixels) {
            pitch=(width*4u+255u)&~255u;
            auto buffer_desc=memory(UINT64(pitch)*height);
            auto readback_heap=heap(D3D12_HEAP_TYPE_READBACK);
            check(device->CreateCommittedResource(&readback_heap,D3D12_HEAP_FLAG_NONE,
                &buffer_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
                IID_PPV_ARGS(&pixels)),"Allocate D3D12 pixel readback");
            barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
            commands->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource=backs[index].Get();
            src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource=pixels.Get();
            dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            dst.PlacedFootprint.Footprint.Width=width;
            dst.PlacedFootprint.Footprint.Height=height;
            dst.PlacedFootprint.Footprint.Depth=1;
            dst.PlacedFootprint.Footprint.RowPitch=pitch;
            commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;
            barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
        } else {
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
        }
        commands->ResourceBarrier(1,&barrier);
        check(commands->Close(),"Close D3D12 command list");
        ID3D12CommandList* lists[]={commands.Get()};
        queue->ExecuteCommandLists(1,lists);
        check(swapchain->Present(1,0),"Present D3D12 frame");
        if(frame.verify_pixels) {
            wait();
            verify_readback(pixels.Get(),pitch);
        }
    }
};
void* create(void* native_window,uint32_t software) {
    Renderer* result=nullptr;
    guarded([&]{result=new Renderer(static_cast<HWND>(native_window),software!=0);});
    return result;
}
void destroy(void* ptr){delete static_cast<Renderer*>(ptr);}
int draw(void* ptr,const NorthStarRenderFrame* frame) {
    return guarded([&]{if(!ptr||!frame)throw std::invalid_argument("Null D3D12 renderer/frame");
        static_cast<Renderer*>(ptr)->draw(*frame);});
}
int set_sky(void* ptr,const NorthStarSky* sky) {
    return guarded([&]{if(!ptr)throw std::invalid_argument("Null D3D12 skydome renderer");
        static_cast<Renderer*>(ptr)->set_sky(sky);});
}
int set_triangles(void* ptr,const NorthStarRenderTriangleVertex* data,UINT count) {
    return guarded([&]{if(!ptr)throw std::invalid_argument("Null D3D12 renderer");
        static_cast<Renderer*>(ptr)->set_triangles(data,count);});
}
int read_memory(void* renderer,NorthStarRenderMemoryStats* result) {
    return guarded([&] {
        if(!renderer||!result)throw std::invalid_argument("Invalid D3D12 memory query");
        *result=static_cast<Renderer const*>(renderer)->memory_stats();
    });
}
const NorthStarRenderMemoryApi memory_api{NORTHSTARCPP_RENDER_MEMORY_ABI,read_memory};
const NorthStarRenderApi api{create,destroy,draw,set_sky};
const NorthStarRenderTrianglesApi triangles_api{
    NORTHSTARCPP_RENDER_TRIANGLES_ABI,set_triangles};
const NorthStarModule module{sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.1.0",NORTHSTARCPP_RENDER_ABI,
    sizeof(api),&api,last_error};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarModule*
northstar_module_query(uint32_t host,const char* capability,uint32_t version) {
    if(version==NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI) {
        static const NorthStarModule extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.1.0",
            NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI,sizeof(triangles_api),
            &triangles_api,last_error};
        return match(extension,host,capability,version);
    }
    if(version==NORTHSTARCPP_RENDER_MEMORY_MODULE_ABI) {
        static const NorthStarModule extension{sizeof(NorthStarModule),
            NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_RENDER,"d3d12","1.1.0",
            NORTHSTARCPP_RENDER_MEMORY_MODULE_ABI,sizeof(memory_api),&memory_api,last_error};
        return match(extension,host,capability,version);
    }
    return match(module,host,capability,version);
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderTrianglesApi*
northstar_render_get_triangles_api(uint32_t version) {
    return version==NORTHSTARCPP_RENDER_TRIANGLES_ABI?&triangles_api:nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarRenderMemoryApi*
northstar_render_get_memory_api(uint32_t version) {
    return version==NORTHSTARCPP_RENDER_MEMORY_ABI?&memory_api:nullptr;
}
