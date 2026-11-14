// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/geometry.hpp>
#include <northstarcpp/compat/core/math_standalone.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/scene.hpp>
#include "module_provider.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <memory>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

using namespace northstarcpp::module_provider;

template <typename T>
class Com {
public:
    ~Com() { reset(); }
    Com() = default;
    Com(Com const&) = delete;
    Com& operator=(Com const&) = delete;
    T* operator->() const { return pointer; }
    T* get() const { return pointer; }
    T** put() { reset(); return &pointer; }
    void reset()
    {
        if (pointer)
            pointer->Release();
        pointer = nullptr;
    }
    void swap(Com& other) noexcept
    {
        std::swap(pointer,other.pointer);
    }

private:
    T* pointer = nullptr;
};

void check(HRESULT result, char const* action)
{
    if (FAILED(result)) {
        char code[20]{};
        std::snprintf(code, sizeof(code), " (0x%08lx)", static_cast<unsigned long>(result));
        throw std::runtime_error(std::string(action) + code);
    }
}

struct Vertex {
    float x, y, z, r, g, b;
};

struct SkyVertex {
    float x, y, z, u, v;
};
static_assert(sizeof(SkyVertex) == sizeof(NorthStarSkyVertex));

constexpr char shader_code[] = R"(
cbuffer Camera : register(b0) { row_major float4x4 view_projection; };
struct Vertex { float3 position : POSITION; float3 color : COLOR; };
struct Pixel { float4 position : SV_Position; float3 color : COLOR; };
Pixel vertex_main(Vertex v) {
    Pixel p;
    p.position = mul(float4(v.position, 1.0), view_projection);
    p.color = v.color;
    return p;
}
float4 pixel_main(Pixel p) : SV_Target {
    return float4(p.color, 1.0);
}
)";

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

void compile_sky(char const* entry, char const* profile, Com<ID3DBlob>& output)
{
    Com<ID3DBlob> errors;
    auto const hr = D3DCompile(sky_shader_code, sizeof(sky_shader_code)-1,
        "skydome", nullptr, nullptr, entry, profile, D3DCOMPILE_ENABLE_STRICTNESS,
        0, output.put(), errors.put());
    if (FAILED(hr) && errors.get())
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(hr, "Compile skydome shader");
}

void compile(char const* entry, char const* profile, Com<ID3DBlob>& output)
{
    Com<ID3DBlob> errors;
    auto const hr = D3DCompile(shader_code, sizeof(shader_code) - 1, "viewport", nullptr,
        nullptr, entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, output.put(), errors.put());
    if (FAILED(hr) && errors.get())
        throw std::runtime_error(static_cast<char const*>(errors->GetBufferPointer()));
    check(hr, "Compile viewport shader");
}

void add_cube(std::vector<Vertex>& vertices, northstarcpp::math::float4x4 const& transform,
    std::uint32_t color)
{
    using namespace northstarcpp::geometry;
    static auto const mesh = primitive(SolidPrimitive::cube);
    constexpr float shade[]{0.74f, 1.f, 0.60f, 0.82f, 0.95f, 0.68f};
    float const r = static_cast<float>((color >> 16) & 255) / 255.f;
    float const g = static_cast<float>((color >> 8) & 255) / 255.f;
    float const b = static_cast<float>(color & 255) / 255.f;
    for (std::size_t triangle = 0; triangle < mesh.index_count; triangle += 3) {
        auto const* a = mesh.positions + 3 * mesh.indices[triangle];
        auto const* point_b = mesh.positions + 3 * mesh.indices[triangle + 1];
        auto const* point_c = mesh.positions + 3 * mesh.indices[triangle + 2];
        unsigned face;
        if (a[1] == point_b[1] && a[1] == point_c[1])
            face = a[1] > 0.f ? 1 : 0;
        else if (a[2] == point_b[2] && a[2] == point_c[2])
            face = a[2] > 0.f ? 4 : 2;
        else
            face = a[0] > 0.f ? 3 : 5;
        for (unsigned corner = 0; corner < 3; ++corner) {
            auto const* point = mesh.positions + 3 * mesh.indices[triangle + corner];
            auto const position = northstarcpp::math::float3(point[0], point[1], point[2]) * transform;
            vertices.push_back({position.x, position.y, position.z,
                r * shade[face], g * shade[face], b * shade[face]});
        }
    }
}

constexpr unsigned grid_vertices = 84;
constexpr unsigned max_vertex_capacity =
    grid_vertices + 36 * northstarcpp::max_scene_objects;
constexpr unsigned min_vertex_capacity = 4096;

struct Renderer {
    explicit Renderer(HWND target, bool software) : window(target)
    {
        HRESULT result = E_FAIL;
        if (!software)
            result = create_device(D3D_DRIVER_TYPE_HARDWARE);
        if (FAILED(result))
            result = create_device(D3D_DRIVER_TYPE_WARP);
        check(result, "Create Direct3D 11 device");

        Com<IDXGIDevice> dxgi_device;
        Com<IDXGIAdapter> adapter;
        Com<IDXGIFactory2> factory;
        check(device->QueryInterface(__uuidof(IDXGIDevice),
            reinterpret_cast<void**>(dxgi_device.put())), "Get DXGI device");
        check(dxgi_device->GetAdapter(adapter.put()), "Get DXGI adapter");
        check(adapter->GetParent(__uuidof(IDXGIFactory2),
            reinterpret_cast<void**>(factory.put())), "Get DXGI factory");

        DXGI_SWAP_CHAIN_DESC1 chain{};
        chain.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        chain.SampleDesc.Count = 1;
        chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        chain.BufferCount = 2;
        chain.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        check(factory->CreateSwapChainForHwnd(device.get(), window, &chain, nullptr, nullptr,
            swap_chain.put()), "Create viewport swap chain");
        check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER),
            "Configure viewport window");

        Com<ID3DBlob> code;
        compile("vertex_main", "vs_4_0", code);
        check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(),
            nullptr, vertex_shader.put()), "Create vertex shader");
        D3D11_INPUT_ELEMENT_DESC const elements[]{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, r), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        check(device->CreateInputLayout(elements, 2, code->GetBufferPointer(), code->GetBufferSize(),
            layout.put()), "Create vertex layout");
        compile("pixel_main", "ps_4_0", code);
        check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(),
            nullptr, pixel_shader.put()), "Create pixel shader");

        // Independent unlit sky program: UVs are authored in the YDD asset.
        compile_sky("sky_vertex_main", "vs_4_0", code);
        check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(),
            nullptr, sky_vertex_shader.put()), "Create skydome vertex shader");
        D3D11_INPUT_ELEMENT_DESC const sky_elements[]{
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(SkyVertex,x),D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(SkyVertex,u),D3D11_INPUT_PER_VERTEX_DATA,0},
        };
        check(device->CreateInputLayout(sky_elements,2,code->GetBufferPointer(),
            code->GetBufferSize(),sky_layout.put()), "Create skydome vertex layout");
        compile_sky("sky_pixel_main", "ps_4_0", code);
        check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(),
            nullptr,sky_pixel_shader.put()), "Create skydome pixel shader");
        D3D11_SAMPLER_DESC sampler_desc{};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&sampler_desc,sky_sampler.put()),
            "Create skydome sampler");
        D3D11_DEPTH_STENCIL_DESC no_depth{};
        no_depth.DepthEnable=FALSE;
        no_depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        no_depth.StencilEnable=FALSE;
        check(device->CreateDepthStencilState(&no_depth,sky_no_depth.put()),
            "Create skydome background depth state");

        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(Vertex) * min_vertex_capacity;
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&buffer, nullptr, vertex_buffer.put()), "Create vertex buffer");
        vertex_buffer_capacity=min_vertex_capacity;
        buffer.ByteWidth = sizeof(northstarcpp::math::float4x4);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer.CPUAccessFlags = 0;
        check(device->CreateBuffer(&buffer, nullptr, camera.put()), "Create camera buffer");
        D3D11_BUFFER_DESC tint_buffer{};
        tint_buffer.ByteWidth = sizeof(float) * 4;
        tint_buffer.Usage = D3D11_USAGE_DEFAULT;
        tint_buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&tint_buffer,nullptr,sky_tint.put()),
            "Create skydome tint buffer");

        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&raster, rasterizer.put()), "Create rasterizer state");
        vertices.reserve(min_vertex_capacity);
    }

    HRESULT create_device(D3D_DRIVER_TYPE driver)
    {
        constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0};
        return D3D11CreateDevice(nullptr, driver, nullptr, 0, levels, 3, D3D11_SDK_VERSION,
            device.put(), nullptr, context.put());
    }

    void resize(unsigned new_width, unsigned new_height)
    {
        if (width == new_width && height == new_height)
            return;
        context->OMSetRenderTargets(0, nullptr, nullptr);
        render_target.reset();
        depth.reset();
        check(swap_chain->ResizeBuffers(0, new_width, new_height, DXGI_FORMAT_UNKNOWN, 0),
            "Resize viewport swap chain");
        Com<ID3D11Texture2D> back_buffer;
        check(swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(back_buffer.put())), "Get viewport buffer");
        check(device->CreateRenderTargetView(back_buffer.get(), nullptr, render_target.put()),
            "Create viewport render target");
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = new_width;
        texture.Height = new_height;
        texture.MipLevels = 1;
        texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        texture.SampleDesc.Count = 1;
        texture.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        Com<ID3D11Texture2D> depth_buffer;
        check(device->CreateTexture2D(&texture, nullptr, depth_buffer.put()), "Create depth buffer");
        check(device->CreateDepthStencilView(depth_buffer.get(), nullptr, depth.put()),
            "Create depth view");
        width = new_width;
        height = new_height;
    }

    void verify_frame()
    {
        Com<ID3D11Texture2D> back_buffer;
        check(swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(back_buffer.put())), "Read viewport buffer");
        D3D11_TEXTURE2D_DESC desc{};
        back_buffer->GetDesc(&desc);
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Com<ID3D11Texture2D> copy;
        check(device->CreateTexture2D(&desc, nullptr, copy.put()), "Create viewport readback");
        context->CopyResource(copy.get(), back_buffer.get());
        D3D11_MAPPED_SUBRESOURCE data{};
        check(context->Map(copy.get(), 0, D3D11_MAP_READ, 0, &data), "Map viewport readback");
        unsigned colored = 0;
        unsigned sky_samples = 0, sky_pixels = 0;
        for (unsigned y = 0; y < height; ++y) {
            auto const* row = static_cast<unsigned char const*>(data.pData) + y * data.RowPitch;
            for (unsigned x = 0; x < width; ++x) {
                auto const* pixel = row + x * 4;
                if (pixel[0] > 110 || pixel[1] > 110 || pixel[2] > 110)
                    ++colored;
                // Validate sky coverage away from health/ammo HUD, and avoid
                // passing merely because world cubes or reticle are colored.
                if (sky_index_count && y < height / 5 &&
                    x > width / 3 && x < width * 2 / 3) {
                    ++sky_samples;
                    if (pixel[0] > 62 || pixel[1] > 62 || pixel[2] > 62)
                        ++sky_pixels;
                }
            }
        }
        context->Unmap(copy.get(), 0);
        if (colored < 100)
            throw std::runtime_error("Viewport readback did not contain a rendered object");
        if (sky_index_count && (!sky_samples || sky_pixels * 10 < sky_samples))
            throw std::runtime_error("Skydome GPU readback lacks textured sky coverage");
    }

    void draw_fps_overlay(NorthStarRenderFrame const& frame)
    {
        // A provider-neutral FPS HUD, independent of depth and world lighting.
        // Primitive clips are stable at every viewport resolution; weapon recoil
        // shifts only the presentation model, never the hitscan ray origin.
        float const hx = 2.f / static_cast<float>(width);
        float const hy = 2.f / static_cast<float>(height);
        std::vector<Vertex> lines;
        std::vector<Vertex> triangles;
        lines.reserve(12);
        triangles.reserve(270);
        auto rect = [&](float x0, float y0, float x1, float y1,
                        float r, float g, float b) {
            triangles.push_back({x0,y0,0.f,r,g,b});
            triangles.push_back({x1,y0,0.f,r,g,b});
            triangles.push_back({x1,y1,0.f,r,g,b});
            triangles.push_back({x0,y0,0.f,r,g,b});
            triangles.push_back({x1,y1,0.f,r,g,b});
            triangles.push_back({x0,y1,0.f,r,g,b});
        };
        auto segment = [&](float x0, float y0, float x1, float y1) {
            constexpr float r = 0.38f, g = 0.94f, b = 0.88f;
            lines.push_back({x0, y0, 0.f, r, g, b});
            lines.push_back({x1, y1, 0.f, r, g, b});
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
        using namespace northstarcpp::math;
        float4x4 identity{float4(1.f,0.f,0.f,0.f),float4(0.f,1.f,0.f,0.f),
                          float4(0.f,0.f,1.f,0.f),float4(0.f,0.f,0.f,1.f)};
        context->UpdateSubresource(camera.get(),0,nullptr,&identity,0,0);
        auto* target=render_target.get();
        context->OMSetRenderTargets(1,&target,nullptr);
        auto batch=[&](std::vector<Vertex> const& values,
                       D3D11_PRIMITIVE_TOPOLOGY topology) {
            if (values.empty()) return;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check(context->Map(vertex_buffer.get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),
                  "Map FPS HUD vertices");
            std::memcpy(mapped.pData,values.data(),values.size()*sizeof(Vertex));
            context->Unmap(vertex_buffer.get(),0);
            context->IASetPrimitiveTopology(topology);
            context->Draw(static_cast<UINT>(values.size()),0);
        };
        batch(triangles,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        batch(lines,D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    }

    void set_sky(NorthStarSky const* source)
    {
        // The render provider owns its D3D resources after this call; no
        // retained pointers to host or AssetManager memory are allowed.
        sky_index_count = 0;
        sky_gpu_bytes=0;
        sky_vertex_buffer.reset();
        sky_index_buffer.reset();
        sky_texture.reset();
        sky_view.reset();
        if (!source) return;
        if (!source->vertices || !source->indices || !source->rgba8 ||
            source->vertex_count < 3 || source->vertex_count > 65536 ||
            source->index_count < 3 || source->index_count > 1000000 ||
            source->index_count % 3 != 0 || source->width == 0 || source->height == 0 ||
            source->width > 8192 || source->height > 8192)
            throw std::runtime_error("Invalid skydome geometry or dimensions");
        for (unsigned i=0; i<source->index_count; ++i)
            if (source->indices[i] >= source->vertex_count)
                throw std::runtime_error("Skydome index exceeds vertex count");
        for (unsigned i=0; i<source->vertex_count; ++i) {
            auto const& v=source->vertices[i];
            if (!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||
                !std::isfinite(v.u)||!std::isfinite(v.v))
                throw std::runtime_error("Skydome vertex contains nonfinite coordinates");
        }
        float const colors[]{source->tint_r,source->tint_g,source->tint_b,1.f};
        for (float component: colors)
            if (!std::isfinite(component) || component < 0.f || component > 4.f)
                throw std::runtime_error("Invalid skydome tint");
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=source->vertex_count * sizeof(SkyVertex);
        desc.Usage=D3D11_USAGE_IMMUTABLE;
        desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertices_data{source->vertices,0,0};
        check(device->CreateBuffer(&desc,&vertices_data,sky_vertex_buffer.put()),
            "Upload skydome YDD vertices");
        desc.ByteWidth=source->index_count * sizeof(uint32_t);
        desc.BindFlags=D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA indices_data{source->indices,0,0};
        check(device->CreateBuffer(&desc,&indices_data,sky_index_buffer.put()),
            "Upload skydome YDD indices");
        D3D11_TEXTURE2D_DESC tex{};
        tex.Width=source->width;
        tex.Height=source->height;
        tex.MipLevels=1;
        tex.ArraySize=1;
        tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        tex.SampleDesc.Count=1;
        tex.Usage=D3D11_USAGE_IMMUTABLE;
        tex.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA tex_data{source->rgba8,source->width*4u,0};
        check(device->CreateTexture2D(&tex,&tex_data,sky_texture.put()),
            "Upload native YTD skydome RGBA8");
        check(device->CreateShaderResourceView(sky_texture.get(),nullptr,sky_view.put()),
            "Create skydome texture view");
        context->UpdateSubresource(sky_tint.get(),0,nullptr,colors,0,0);
        sky_index_count=source->index_count;
        sky_gpu_bytes=static_cast<std::uint64_t>(source->vertex_count)*sizeof(SkyVertex)+
            static_cast<std::uint64_t>(source->index_count)*sizeof(std::uint32_t)+
            static_cast<std::uint64_t>(source->width)*source->height*4u;
    }

    NorthStarRenderMemoryStats memory_stats() const
    {
        NorthStarRenderMemoryStats stats{};
        stats.vertex_buffer_bytes=
            static_cast<std::uint64_t>(vertex_buffer_capacity)*sizeof(Vertex);
        stats.sky_geometry_and_texture_bytes=sky_gpu_bytes;
        stats.vertex_buffer_reallocations=vertex_buffer_reallocations;
        stats.world_vertex_capacity=vertex_buffer_capacity;
        return stats;
    }

    void draw_sky(northstarcpp::math::float3 const& direction,
                  northstarcpp::math::float3 const& up,
                  northstarcpp::math::float4x4 const& projection)
    {
        if (!sky_index_count) return;
        using namespace northstarcpp::math;
        // Rotation only: translation is removed from the sky view so that the
        // authored dome always follows the camera without parallax.
        auto const view=create_camera_at(float3(0.f,0.f,0.f),direction,up);
        auto const vp=mul4x4(view,projection);
        context->UpdateSubresource(camera.get(),0,nullptr,&vp,0,0);
        UINT const stride=sizeof(SkyVertex), offset=0;
        auto* buffer=sky_vertex_buffer.get();
        auto* index=sky_index_buffer.get();
        auto* vertex=sky_vertex_shader.get();
        auto* pixel=sky_pixel_shader.get();
        auto* texture=sky_view.get();
        auto* sampler=sky_sampler.get();
        auto* constants=sky_tint.get();
        context->IASetInputLayout(sky_layout.get());
        context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        context->IASetIndexBuffer(index,DXGI_FORMAT_R32_UINT,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex,nullptr,0);
        auto* camera_buffer=camera.get();
        context->VSSetConstantBuffers(0,1,&camera_buffer);
        context->PSSetShader(pixel,nullptr,0);
        context->PSSetConstantBuffers(1,1,&constants);
        context->PSSetShaderResources(0,1,&texture);
        context->PSSetSamplers(0,1,&sampler);
        context->OMSetDepthStencilState(sky_no_depth.get(),0);
        context->DrawIndexed(sky_index_count,0,0);
        ID3D11ShaderResourceView* empty=nullptr;
        context->PSSetShaderResources(0,1,&empty);
        context->OMSetDepthStencilState(nullptr,0);
    }

    void ensure_vertex_capacity(std::size_t required)
    {
        // HLOD and streaming reduce this transient GPU vertex allocation.
        // Shrink only after 120 consecutive underutilized frames to avoid
        // buffer thrash when cells load/unload at an LOD boundary.
        if (required>max_vertex_capacity)
            throw std::runtime_error("Render frame exceeds vertex capacity");
        std::size_t next=vertex_buffer_capacity;
        if (required>next) {
            do {
                next=std::min<std::size_t>(max_vertex_capacity,next*2);
            } while (next<required);
            underutilized_frames=0;
        } else if (next>min_vertex_capacity && required*4<next) {
            if (++underutilized_frames>=120) {
                next=min_vertex_capacity;
                while (next<required*2 && next<max_vertex_capacity)
                    next=std::min<std::size_t>(max_vertex_capacity,next*2);
                underutilized_frames=0;
            }
        } else {
            underutilized_frames=0;
        }
        if (next==vertex_buffer_capacity)
            return;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=static_cast<UINT>(next*sizeof(Vertex));
        desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        // Allocate first, then replace atomically. On allocation failure the
        // previous frame buffer remains valid and its COM ownership intact.
        Com<ID3D11Buffer> replacement;
        check(device->CreateBuffer(&desc,nullptr,replacement.put()),
              "Reallocate residency-aware D3D11 vertex buffer");
        vertex_buffer.swap(replacement);
        vertex_buffer_capacity=static_cast<unsigned>(next);
        ++vertex_buffer_reallocations;
    }

    void set_triangles(NorthStarRenderTriangleVertex const* src,std::uint32_t count)
    {
        constexpr std::size_t limit=300000;
        if (count>limit || count%3!=0 || (count && !src))
            throw std::invalid_argument("Invalid static mesh triangle buffer");
        std::vector<Vertex> submitted;
        submitted.reserve(count);
        for (std::uint32_t i=0;i<count;++i) {
            auto const& v=src[i];
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
                std::max({std::abs(v.x),std::abs(v.y),std::abs(v.z)})>1000000.f)
                throw std::invalid_argument("Mesh vertex position out of range");
            auto const rgb=v.rgb;
            submitted.push_back({v.x,v.y,v.z,
                float((rgb>>16)&255u)/255.f,
                float((rgb>>8)&255u)/255.f,
                float(rgb&255u)/255.f});
        }
        mesh_triangles.swap(submitted);
    }

    void draw(NorthStarRenderFrame const& frame)
    {
        RECT client{};
        GetClientRect(window, &client);
        if (client.right <= 0 || client.bottom <= 0)
            return;
        resize(client.right, client.bottom);

        vertices.clear();
        for (int n = -10; !frame.first_person && n <= 10; ++n) {
            auto const i = static_cast<float>(n);
            float const tone = n == 0 ? 0.38f : 0.23f;
            vertices.push_back({i, -0.01f, -10.f, tone, tone, tone});
            vertices.push_back({i, -0.01f, 10.f, tone, tone, tone});
            vertices.push_back({-10.f, -0.01f, i, tone, tone, tone});
            vertices.push_back({10.f, -0.01f, i, tone, tone, tone});
        }
        if (frame.count > northstarcpp::max_scene_objects || (frame.count && !frame.cubes))
            throw std::runtime_error("Invalid render cube buffer");
        using namespace northstarcpp::math;
        float3 const focus(frame.camera_x, frame.camera_y, frame.camera_z);
        auto const direction = float3(std::sin(frame.camera_yaw) * std::cos(frame.camera_pitch),
            std::sin(frame.camera_pitch), std::cos(frame.camera_yaw) * std::cos(frame.camera_pitch));
        if (frame.camera_override &&
            (!std::isfinite(frame.camera_yaw) ||
             !std::isfinite(frame.camera_pitch) ||
             !std::isfinite(frame.camera_roll) ||
             !std::isfinite(frame.camera_fov_y_degrees) ||
             !std::isfinite(frame.camera_near_clip) ||
             !std::isfinite(frame.camera_far_clip) ||
             frame.camera_fov_y_degrees < 25.f ||
             frame.camera_fov_y_degrees > 120.f ||
             frame.camera_near_clip < 0.01f ||
             frame.camera_far_clip <= frame.camera_near_clip + 1.f))
            throw std::runtime_error("Invalid CameraDirector render frame");
        auto const right = float3(std::cos(frame.camera_yaw), 0.f,
                                  -std::sin(frame.camera_yaw));
        auto const base_up = direction ^ right;
        auto const up = frame.camera_override
            ? base_up * std::cos(frame.camera_roll) +
              right * std::sin(frame.camera_roll)
            : float3(0.f, 1.f, 0.f);
        auto const view = frame.camera_override || frame.first_person
            ? create_camera_at(focus, focus + direction, up)
            : create_camera_at(focus + float3(8.f, 7.f, -10.f), focus,
                               {0.f, 1.f, 0.f});
        constexpr float rad_per_degree = 3.14159265358979323846f / 180.f;
        auto const fov = frame.camera_override
            ? frame.camera_fov_y_degrees * rad_per_degree : pi_d4;
        auto const projection = create_projection(fov,
            static_cast<float>(height) / width,
            frame.camera_override ? frame.camera_near_clip : 0.1f,
            frame.camera_override ? frame.camera_far_clip :
                (std::isfinite(frame.camera_far_clip) && frame.camera_far_clip>250.f &&
                 frame.camera_far_clip<=20000.f ? frame.camera_far_clip : 250.f));
        auto const view_projection = mul4x4(view, projection);
        frustum const visible_region(view_projection, clip_depth::zero_to_one);
        for (std::uint32_t i = 0; i < frame.count; ++i) {
            auto const& cube = frame.cubes[i];
            auto const* axes = cube.rotation;
            float const half = cube.size * 0.5f;
            float4x4 transform;
            transform.i = float4(axes[0] * half, axes[1] * half, axes[2] * half, 0.f);
            transform.j = float4(axes[3] * half, axes[4] * half, axes[5] * half, 0.f);
            transform.k = float4(axes[6] * half, axes[7] * half, axes[8] * half, 0.f);
            transform.c = float4(cube.x, cube.y + half, cube.z, 1.f);
            aabb bounds;
            bounds.min = {-1.f, -1.f, -1.f};
            bounds.max = {1.f, 1.f, 1.f};
            bounds.modify(transform);
            // Leave a margin for CPU and GPU rounding at the clip planes.
            bounds += 0.05f;
            if (visible_region.test_inexact(bounds) == intersection_outside)
                continue;
            add_cube(vertices, transform, static_cast<std::int32_t>(i) == frame.selected ? 0xffd474u : cube.rgb);
        }
        if (mesh_triangles.size()>max_vertex_capacity-vertices.size())
            throw std::runtime_error("Mesh geometry exceeds transient GPU budget");
        vertices.insert(vertices.end(),mesh_triangles.begin(),mesh_triangles.end());
        context->UpdateSubresource(camera.get(), 0, nullptr, &view_projection, 0, 0);
        ensure_vertex_capacity(vertices.size());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped),
            "Map viewport vertices");
        std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(Vertex));
        context->Unmap(vertex_buffer.get(), 0);

        auto* target = render_target.get();
        context->OMSetRenderTargets(1, &target, depth.get());
        float const clear[]{0.055f, 0.075f, 0.105f, 1.f};
        context->ClearRenderTargetView(target, clear);
        context->ClearDepthStencilView(depth.get(), D3D11_CLEAR_DEPTH, 1.f, 0);
        D3D11_VIEWPORT const viewport{0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
        context->RSSetViewports(1, &viewport);
        context->RSSetState(rasterizer.get());
        // Background pass runs before world geometry, using the actual YDD mesh,
        // native YMAT material tint and YTD texture. Sky never writes depth.
        if (sky_index_count)
            draw_sky(direction,up,projection);
        context->UpdateSubresource(camera.get(),0,nullptr,&view_projection,0,0);

        UINT const stride = sizeof(Vertex), offset = 0;
        auto* buffer = vertex_buffer.get();
        auto* constants = camera.get();
        context->IASetInputLayout(layout.get());
        context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
        context->VSSetShader(vertex_shader.get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &constants);
        context->PSSetShader(pixel_shader.get(), nullptr, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        if (!frame.first_person) {
            context->Draw(grid_vertices, 0);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->Draw(static_cast<UINT>(vertices.size()) - grid_vertices, grid_vertices);
        } else {
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->Draw(static_cast<UINT>(vertices.size()), 0);
            if (frame.player_max_health != 0u)
                draw_fps_overlay(frame);
        }

        if (frame.verify_pixels)
            verify_frame();
        check(swap_chain->Present(1, 0), "Present viewport frame");
    }

    HWND window;
    unsigned width = 0, height = 0;
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<IDXGISwapChain1> swap_chain;
    Com<ID3D11RenderTargetView> render_target;
    Com<ID3D11DepthStencilView> depth;
    Com<ID3D11VertexShader> vertex_shader;
    Com<ID3D11PixelShader> pixel_shader;
    Com<ID3D11InputLayout> layout;
    Com<ID3D11Buffer> vertex_buffer;
    unsigned vertex_buffer_capacity=0;
    unsigned underutilized_frames=0;
    unsigned vertex_buffer_reallocations=0;
    Com<ID3D11Buffer> camera;
    Com<ID3D11RasterizerState> rasterizer;
    Com<ID3D11VertexShader> sky_vertex_shader;
    Com<ID3D11PixelShader> sky_pixel_shader;
    Com<ID3D11InputLayout> sky_layout;
    Com<ID3D11SamplerState> sky_sampler;
    Com<ID3D11DepthStencilState> sky_no_depth;
    Com<ID3D11Buffer> sky_tint, sky_vertex_buffer, sky_index_buffer;
    Com<ID3D11Texture2D> sky_texture;
    Com<ID3D11ShaderResourceView> sky_view;
    unsigned sky_index_count = 0;
    std::uint64_t sky_gpu_bytes=0;
    std::vector<Vertex> vertices;
    std::vector<Vertex> mesh_triangles;
};

void* create(void* window, uint32_t software)
{
    Renderer* result = nullptr;
    guarded([&] { result = new Renderer(static_cast<HWND>(window), software != 0); });
    return result;
}

void destroy(void* renderer) { delete static_cast<Renderer*>(renderer); }

int draw(void* renderer, NorthStarRenderFrame const* frame)
{
    return guarded([&] {
        if (!renderer || !frame)
            throw std::runtime_error("Missing renderer or frame");
        static_cast<Renderer*>(renderer)->draw(*frame);
    });
}

int set_sky(void* renderer, NorthStarSky const* sky)
{
    return guarded([&] {
        if (!renderer)
            throw std::runtime_error("Missing skydome renderer");
        static_cast<Renderer*>(renderer)->set_sky(sky);
    });
}

int read_memory_stats(void* renderer,NorthStarRenderMemoryStats* result)
{
    return guarded([&] {
        if (!renderer || !result)
            throw std::invalid_argument("Invalid D3D11 memory stats query");
        *result=static_cast<Renderer const*>(renderer)->memory_stats();
    });
}
int set_render_triangles(void* renderer,
                         NorthStarRenderTriangleVertex const* vertices,std::uint32_t count)
{
    return guarded([&] {
        if (!renderer) throw std::invalid_argument("Missing geometry renderer");
        static_cast<Renderer*>(renderer)->set_triangles(vertices,count);
    });
}
const NorthStarRenderTrianglesApi triangles_api{
    NORTHSTARCPP_RENDER_TRIANGLES_ABI,set_render_triangles};
const NorthStarRenderMemoryApi memory_api{
    NORTHSTARCPP_RENDER_MEMORY_ABI,read_memory_stats};
const NorthStarRenderApi api{create, destroy, draw, set_sky};
const NorthStarModule module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER, "Direct3D 11", "5.0.0", NORTHSTARCPP_RENDER_ABI,
    sizeof(api), &api, last_error};

} // namespace

extern "C" const NorthStarModule* northstar_module_query(uint32_t host,
    char const* capability, uint32_t version)
{
    if (version == NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER, "Direct3D 11", "5.0.0",
            NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI, sizeof(triangles_api), &triangles_api, last_error};
        return match(extension, host, capability, version);
    }
    if (version == NORTHSTARCPP_RENDER_MEMORY_MODULE_ABI) {
        static NorthStarModule const extension{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER, "Direct3D 11", "5.0.0",
            NORTHSTARCPP_RENDER_MEMORY_MODULE_ABI, sizeof(memory_api), &memory_api, last_error};
        return match(extension, host, capability, version);
    }
    return match(module, host, capability, version);
}

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarRenderTrianglesApi const* northstar_render_get_triangles_api(uint32_t version)
{
    return version==NORTHSTARCPP_RENDER_TRIANGLES_ABI ? &triangles_api : nullptr;
}

extern "C" NORTHSTARCPP_MODULE_EXPORT
NorthStarRenderMemoryApi const* northstar_render_get_memory_api(uint32_t version)
{
    return version==NORTHSTARCPP_RENDER_MEMORY_ABI ? &memory_api : nullptr;
}
