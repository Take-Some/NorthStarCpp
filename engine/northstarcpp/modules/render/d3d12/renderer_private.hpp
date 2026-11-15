#pragma once
// NorthStarCpp - native D3D12 provider, stable engine.render ABI 5.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/render_plugin.h>
#include <ui_quad.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_lights_api.h>
#include "../shared/tiled_lights.hpp"
#include "../shared/material_hlsl.hpp"
#include "../shared/material_variants.hpp"
#include "../shared/deferred_math.hpp"
#include "../shared/cascaded_shadows.hpp"
#include <northstarcpp/geometry.hpp>
#include <northstarcpp/core/math_standalone.h>
#include <northstarcpp/scene.hpp>
#include "module_provider.hpp"
#include "shader_compiler.hpp"
#include "renderer_geometry.hpp"
#include "renderer_resource_states.hpp"
#include "renderer_texture_pool.hpp"
#include "renderer_transient_alias.hpp"
#include "renderer_pass_graph.hpp"
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
#include <array>
#include <memory>
#include <unordered_map>

namespace northstarcpp::render::d3d12::detail {
using namespace northstarcpp::module_provider;
using northstarcpp::render::d3d12::geometry::Vertex;
using northstarcpp::render::d3d12::geometry::make_hud;
using northstarcpp::render::d3d12::geometry::cube;
using Microsoft::WRL::ComPtr;
using northstarcpp::shaders::authored_material_shader;
using northstarcpp::render::d3d12::shader_compiler::compile;
using northstarcpp::render::d3d12::shader_compiler::compile_sky;
using northstarcpp::render::d3d12::shader_compiler::compile_authored_material;
using northstarcpp::render::d3d12::shader_compiler::compile_material_variant;
using northstarcpp::render::d3d12::shader_compiler::compile_deferred;
using northstarcpp::render::d3d12::shader_compiler::compile_fxaa;
using northstarcpp::render::d3d12::shader_compiler::compile_bloom;
using northstarcpp::render::d3d12::shader_compiler::compile_hdr;
using northstarcpp::render::d3d12::shader_compiler::compile_shadow;
inline void check(HRESULT hr,const char* name) {
    if(FAILED(hr)) {char error[160];
        std::snprintf(error,sizeof(error),"%s: 0x%08lx",name,static_cast<unsigned long>(hr));
        throw std::runtime_error(error);}
}

struct MaterialParams {
    float base_color[4];
    float factors[4];
    std::uint32_t present[4];
};
static_assert(sizeof(MaterialParams)==48);
struct MaterialGpuTexture {
    ComPtr<ID3D12Resource> texture;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
};
struct MaterialDraw {
    ComPtr<ID3D12PipelineState> variant_pipeline;
    unsigned blend_mode=0;
    UINT first=0,count=0;
    MaterialParams params{};
    std::array<std::shared_ptr<MaterialGpuTexture>,5> maps{};
};
struct MaterialTextureUpload {
    ComPtr<ID3D12Resource> staging;
    std::shared_ptr<MaterialGpuTexture> texture;
    UINT width=0,height=0,pitch=0;
};

constexpr UINT capacity=400000,buffer_count=2;
inline D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h{};h.Type=type;h.CreationNodeMask=h.VisibleNodeMask=1;return h;
}
inline D3D12_RESOURCE_DESC memory(UINT64 bytes) {
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width=bytes;d.Height=d.DepthOrArraySize=d.MipLevels=1;
    d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return d;
}
struct SkyVertex {float x,y,z,u,v;};
static_assert(sizeof(SkyVertex)==sizeof(NorthStarSkyVertex));
// Pass inputs borrow the stack-owned draw() context. No cross-frame pointers.
struct FramePassContext {
    const NorthStarRenderFrame& frame;
    const northstarcpp::math::float4x4& matrix;
    const northstarcpp::math::float3& focus;
    const northstarcpp::math::float3& camera_forward;
    const northstarcpp::render::ShadowCascadePlan& csm;
    const std::vector<Vertex>& vertices;
    UINT grid,hud_tri_start,hud_line_start,index;
    bool has_deferred_geometry;
    D3D12ResourceStates& states;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv,scene_rtv,dsv;
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissors;
};
struct RendererState {
    D3D12TextureReusePool texture_reuse_pool;
    D3D12PlacedTransientAlias transient_alias;
    bool transient_aliasing_enabled=false;
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
    ComPtr<ID3D12PipelineState> triangles,lines,gbuffer_triangles,hdr_lines;
    D3D12_VERTEX_BUFFER_VIEW vertex_view{};
    unsigned char* vertex_data=nullptr;
    unsigned char* camera_data=nullptr;
    std::vector<Vertex> mesh_vertices;
    ComPtr<ID3D12RootSignature> material_signature;
    ComPtr<ID3D12PipelineState> material_pipeline;
    std::unordered_map<std::string,ComPtr<ID3D12PipelineState>>
        material_shader_cache;
    ComPtr<ID3D12RootSignature> deferred_signature;
    ComPtr<ID3D12PipelineState> deferred_pipeline,hdr_deferred_pipeline;
    ComPtr<ID3D12RootSignature> fxaa_signature;
    ComPtr<ID3D12PipelineState> fxaa_pipeline,bloom_pipeline,
                                 bloom_composite_pipeline;
    ComPtr<ID3D12PipelineState> hdr_exposure_pipeline,hdr_tonemap_pipeline;
    ComPtr<ID3D12DescriptorHeap> hdr_rtv_heap,hdr_exposure_rtv_heap,
        hdr_srvs[2],hdr_exposure_srvs[2],hdr_bloom_srvs;
    ComPtr<ID3D12Resource> hdr_scene_color,hdr_exposure[2];
    UINT hdr_exposure_rtv_stride=0;
    UINT exposure_frame_index=0;
    bool exposure_history_valid=false;
    bool hdr_enabled=false;
    ComPtr<ID3D12DescriptorHeap> bloom_rtv_heap;
    ComPtr<ID3D12Resource> bloom_surface;
    bool bloom_enabled=false;
    bool shadows_enabled=false;
    bool pcss_enabled=false;
    UINT sun_shadow_resolution=1024; // WARP uses cheaper 256px QA map
    static constexpr UINT cascade_count=northstarcpp::render::shadow_cascade_count;
    UINT shadow_dsv_stride=0;
    ComPtr<ID3D12PipelineState> shadow_geometry_pso,shadow_material_pso;
    ComPtr<ID3D12DescriptorHeap> sun_shadow_dsv_heap;
    ComPtr<ID3D12Resource> sun_shadow_map,sun_shadow_constants;
    unsigned char* sun_shadow_constants_data=nullptr;
    bool postfx_enabled() const noexcept {
        return fxaa_enabled||bloom_enabled||hdr_enabled;
    }
    ComPtr<ID3D12DescriptorHeap> scene_rtv_heap,scene_srv_heap;
    ComPtr<ID3D12Resource> scene_color,fxaa_constants;
    unsigned char* fxaa_constant_data=nullptr;
    bool fxaa_enabled=true;
    bool ssao_enabled=false;
    ComPtr<ID3D12DescriptorHeap> deferred_rtvs,deferred_srvs;
    ComPtr<ID3D12Resource> gbuffer[3],deferred_constants;
    unsigned char* deferred_constant_data=nullptr;
    UINT deferred_rtv_stride=0,deferred_srv_stride=0;
    std::vector<NorthStarRenderLight> dynamic_lights;
    ComPtr<ID3D12Resource> dynamic_light_buffer,tile_index_buffer;
    unsigned char* dynamic_light_bytes=nullptr;
    unsigned char* tile_index_bytes=nullptr;
    std::uint32_t tile_buffer_entries=0;
    ComPtr<ID3D12DescriptorHeap> material_srv_heap;
    ComPtr<ID3D12Resource> material_vertex_gpu,material_constant_gpu,material_camera_gpu;
    unsigned char* material_vertex_data=nullptr;
    unsigned char* material_constant_data=nullptr;
    unsigned char* material_camera_data=nullptr;
    D3D12_VERTEX_BUFFER_VIEW material_vertex_view{};
    std::vector<NorthStarMaterialVertex> material_cpu_vertices;
    std::vector<MaterialDraw> material_draws;
    std::unordered_map<std::uint64_t,std::shared_ptr<MaterialGpuTexture>> material_texture_cache;
    std::uint64_t material_cache_bytes=0;
    UINT material_descriptor_increment=0;
    ComPtr<ID3D12RootSignature> sky_signature;
    ComPtr<ID3D12PipelineState> sky_pipeline,hud_triangles_pso,
                                  hud_lines_pso,hdr_sky_pipeline;
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
    ComPtr<ID3D12QueryHeap> timestamp_heap;
    ComPtr<ID3D12Resource> timestamp_readback;
    UINT64 timestamp_frequency=0;
    bool timestamp_pending=false;
    static constexpr UINT timestamp_count=5;

    RendererState(HWND window,bool software);
    ~RendererState();
    void wait();
    void resize(UINT w,UINT h);
    void ensure_gbuffer();
    void ensure_light_buffers();
    void set_lights(NorthStarRenderLight const* data,UINT count);
    void ensure_material_buffers();
    std::shared_ptr<MaterialGpuTexture> material_texture(
        NorthStarMaterialTexture const& image,bool is_srgb,
        std::vector<MaterialTextureUpload>& pending);
    void set_material_batches(NorthStarMaterialBatch const* batches,UINT count);
    ComPtr<ID3D12PipelineState> material_graph_pipeline(
        std::string const& prefix,unsigned blend_mode);
    void set_material_batches_v2(NorthStarMaterialBatchV2 const* batches,UINT count);
    void set_triangles(const NorthStarRenderTriangleVertex* source,UINT count);
    void set_sky(NorthStarSky const* source);
    NorthStarRenderTimingStats timing_stats();
    NorthStarRenderMemoryStats memory_stats() const;
    void verify_readback(ID3D12Resource* pixels,UINT pitch,UINT mode);
    void draw_translucent_materials();
    void draw(const NorthStarRenderFrame& frame);
    void apply_graph_resource_states(
        const northstarcpp::render::ScheduledPass& pass,FramePassContext& context);
    void record_shadow_pass(FramePassContext& context);
    void record_gbuffer_pass(FramePassContext& context);
    void record_deferred_pass(FramePassContext& context);
    void record_translucency_pass(FramePassContext& context);
    void record_bloom_pass(FramePassContext& context);
    void record_exposure_pass(FramePassContext& context);
    void record_tonemap_pass(FramePassContext& context);
    void record_composite_pass(FramePassContext& context);
    void record_present_pass(FramePassContext& context);

};
} // namespace northstarcpp::render::d3d12::detail
