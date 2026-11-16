#pragma once
// NorthStarCpp - native D3D12 provider, stable engine.render ABI 5.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_ui_api.h>
#include <northstarcpp/render_postfx_api.h>
#include "../shared/postfx_look.hpp"
#include <northstarcpp/render_editor_grid_api.h>
#include "../shared/editor_grid_constants.hpp"
#include "../shared/editor_grid_hlsl.hpp"
#include <ui_quad.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_shader_reload_api.h>
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_pick_api.h>
#include <northstarcpp/render_lights_api.h>
#include "../shared/tiled_lights.hpp"
#include "../shared/tiled_light_cache.hpp"
#include "../shared/temporal_history.hpp"
#include "../shared/material_hlsl.hpp"
#include "../shared/material_variants.hpp"
#include "../shared/deferred_math.hpp"
#include "../shared/gbuffer_semantics.hpp"
#include "../shared/cascaded_shadows.hpp"
#include <northstarcpp/geometry.hpp>
#include <northstarcpp/core/math_standalone.h>
#include <northstarcpp/scene.hpp>
#include "module_provider.hpp"
#include "shader_compiler.hpp"
#include "renderer_geometry.hpp"
#include "renderer_resource_states.hpp"
#include "renderer_texture_pool.hpp"
#include "renderer_upload_pages.hpp"
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
#include <unordered_set>
#include <future>
#include <deque>
#include <string>
#include <chrono>

namespace northstarcpp::render::d3d12::detail {
using namespace northstarcpp::module_provider;
using northstarcpp::render::d3d12::geometry::Vertex;
using northstarcpp::render::d3d12::geometry::make_hud;
using northstarcpp::render::d3d12::geometry::cube;
using Microsoft::WRL::ComPtr;
using northstarcpp::shaders::authored_material_shader;
using northstarcpp::render::d3d12::shader_compiler::compile;
using northstarcpp::render::d3d12::shader_compiler::compile_editor_grid;
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
    UINT width=0,height=0;
    std::vector<std::uint8_t> source;
};
ComPtr<ID3D12PipelineState> create_material_graph_pso(
    ComPtr<ID3D12Device> device, ComPtr<ID3D12RootSignature> signature,
    std::string const& prefix,unsigned blend_mode,bool hdr_enabled,bool gbuffer_v2,
    const std::vector<std::uint8_t>& vertex={},
    const std::vector<std::uint8_t>& pixel={});
struct MaterialDraw {
    ComPtr<ID3D12PipelineState> variant_pipeline;
    std::string variant_prefix;
    unsigned blend_mode=0;
    UINT first=0,count=0;
    MaterialParams params{};
    std::array<std::shared_ptr<MaterialGpuTexture>,5> maps{};
};
struct MaterialTextureUpload {
    UploadSlice staging;
    std::shared_ptr<MaterialGpuTexture> texture;
    UINT width=0,height=0,pitch=0;
    std::uint64_t fingerprint=0;
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
    NorthStarRenderEditorGridSettings editor_grid{};
    ComPtr<ID3D12RootSignature> grid_signature;
    ComPtr<ID3D12PipelineState> grid_pipeline,grid_hdr_pipeline;
    ComPtr<ID3D12PipelineState> grid_temporal_pipeline;
    ComPtr<ID3D12Resource> grid_constant_buffer,grid_previous_buffer;
    unsigned char* grid_constant_data=nullptr;
    unsigned char* grid_previous_data=nullptr;
    ComPtr<ID3D12DescriptorHeap> grid_history_rtvs,grid_history_srvs;
    std::array<ComPtr<ID3D12Resource>,2> grid_history_textures;
    northstarcpp::render::EditorGridConstants grid_previous_values{};
    unsigned grid_history_index=0,grid_frame_index=0;
    bool grid_history_valid=false;
    UINT grid_rtv_stride=0,grid_srv_stride=0;
    ComPtr<ID3D12PipelineState> triangles,lines,gbuffer_triangles,hdr_lines;
    D3D12_VERTEX_BUFFER_VIEW vertex_view{};
    unsigned char* vertex_data=nullptr;
    unsigned char* camera_data=nullptr;
    std::vector<Vertex> mesh_vertices;
    struct PickCube {NorthStarRenderCube cube{};std::uint32_t index=0;};
    std::vector<PickCube> pick_cubes;
    std::vector<NorthStarRenderPickVertex> pick_world_vertices;
    std::vector<std::uint64_t> pick_cube_ids;
    bool pick_entity_mode=false;
    std::uint64_t last_picked_entity=0;
    // Dedicated on-demand GPU queue: no host wait on GUI click/poll.
    // Each slot owns its command allocator, camera/vertex snapshot, target
    // and staging readback until its fence signals (including cancellation).
    struct AsyncPickSlot {
        bool active=false;
        bool target_in_copy_state=false;
        std::uint64_t request_id=0,scene_generation=0,fence_value=0;
        UINT width=0,height=0,vertex_capacity=0;
        std::vector<std::uint64_t> tokens;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        ComPtr<ID3D12Resource> vertex_upload,camera_upload;
        ComPtr<ID3D12Resource> target,depth,readback;
        ComPtr<ID3D12DescriptorHeap> rtv,dsv;
        D3D12_VERTEX_BUFFER_VIEW vertex_view{};
    };
    std::array<AsyncPickSlot,3> async_pick_slots{};
    ComPtr<ID3D12CommandQueue> async_pick_queue;
    ComPtr<ID3D12Fence> async_pick_fence;
    std::uint64_t next_async_pick_fence=0;
    std::uint64_t minimum_pick_generation=0;
    bool enqueue_async_pick(NorthStarRenderPickAsyncRequest const&);
    NorthStarRenderPickAsyncResult poll_async_pick(std::uint64_t);
    void invalidate_async_pick(std::uint64_t) noexcept;
    void shutdown_async_pick() noexcept;
    ComPtr<ID3D12RootSignature> material_signature;
    ComPtr<ID3D12PipelineState> material_pipeline;
    std::unordered_map<std::string,ComPtr<ID3D12PipelineState>>
        material_shader_cache;
    // Bounded nonblocking async variant workers. Futures own the device and
    // root signature while compiling, never RendererState or draw pointers.
    std::unordered_map<std::string,
        std::future<ComPtr<ID3D12PipelineState>>> material_pipeline_pending;
    std::unordered_set<std::string> material_pipeline_failed;
    struct HotShader {
        std::array<std::vector<std::uint8_t>,3> binaries;
        ComPtr<ID3D12PipelineState> installed;
        std::string prefix,error;
        unsigned blend_mode=0,state=NS_SHADER_RELOAD_UNKNOWN;
        std::uint64_t revision=0,installed_revision=0;
    };
    struct HotBuild {
        ComPtr<ID3D12PipelineState> pipeline;
        std::string error;
    };
    std::unordered_map<std::string,HotShader> hot_shaders;
    std::vector<std::string> hot_references;
    std::deque<std::string> hot_queue;
    std::future<HotBuild> hot_worker;
    std::string hot_active_key;
    std::uint64_t hot_active_revision=0,hot_sequence=0;
    void set_hot_material_references(const char* const*,std::uint32_t);
    void queue_hot_shader(const char*,std::uint32_t,std::uint32_t,
                          const std::uint8_t*,std::uint32_t);
    bool poll_hot_shader(const char*,NorthStarShaderReloadStatus&) const;
    void pump_hot_shaders();
    ID3D12PipelineState* hot_pipeline(std::size_t index) const noexcept;
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
    std::chrono::steady_clock::time_point exposure_frame_clock{};
    bool exposure_frame_clock_valid=false;
    northstarcpp::render::TemporalHistoryTracker temporal_tracker;
    northstarcpp::render::TemporalFrameState temporal_frame{};
    bool hdr_enabled=false;
    ComPtr<ID3D12DescriptorHeap> bloom_rtv_heap;
    ComPtr<ID3D12Resource> bloom_surface;
    bool bloom_enabled=false;
    bool film_look_resources_requested=false;
    NorthStarRenderPostFxSettings film_look=northstarcpp::render::neutral_postfx();
    std::uint32_t film_frame=0;
    void set_film_look(NorthStarRenderPostFxSettings const& settings);
    NorthStarRenderPostFxSettings get_film_look() const noexcept {return film_look;}
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
        return fxaa_enabled||bloom_enabled||hdr_enabled||
               film_look_resources_requested;
    }
    ComPtr<ID3D12DescriptorHeap> scene_rtv_heap,scene_srv_heap;
    ComPtr<ID3D12Resource> scene_color,fxaa_constants;
    unsigned char* fxaa_constant_data=nullptr;
    bool fxaa_enabled=true;
    bool ssao_enabled=false;
    // Hardware GBuffer inspection without extra MRTs or readback stalls.
    unsigned gbuffer_debug_mode=0;
    ComPtr<ID3D12DescriptorHeap> deferred_rtvs,deferred_srvs;
    ComPtr<ID3D12Resource> gbuffer[4],deferred_constants;
    bool gbuffer_v2=false;
    unsigned char* deferred_constant_data=nullptr;
    UINT deferred_rtv_stride=0,deferred_srv_stride=0;
    std::vector<NorthStarRenderLight> dynamic_lights;
    ComPtr<ID3D12Resource> dynamic_light_buffer,tile_index_buffer;
    unsigned char* dynamic_light_bytes=nullptr;
    unsigned char* tile_index_bytes=nullptr;
    std::uint32_t tile_buffer_entries=0;
    northstarcpp::render::TileLightCache tile_light_cache;
    ComPtr<ID3D12DescriptorHeap> material_srv_heap;
    ComPtr<ID3D12Resource> material_vertex_gpu,material_constant_gpu,material_camera_gpu;
    unsigned char* material_vertex_data=nullptr;
    unsigned char* material_constant_data=nullptr;
    unsigned char* material_camera_data=nullptr;
    D3D12_VERTEX_BUFFER_VIEW material_vertex_view{};
    std::size_t material_vertex_capacity=0;
    std::size_t material_batch_capacity=64;
    std::vector<NorthStarMaterialVertex> material_cpu_vertices,material_vertex_scratch;
    bool material_vertices_dirty=true;
    std::vector<MaterialDraw> material_draws;
    std::unordered_multimap<std::uint64_t,std::shared_ptr<MaterialGpuTexture>> material_texture_cache;
    std::uint64_t material_cache_bytes=0;
    UploadPages upload_pages;
    // Retains copy destinations if signaling or waiting fails after submission.
    std::vector<MaterialTextureUpload> material_uploads_in_flight;
    UINT material_descriptor_increment=0;
    ComPtr<ID3D12RootSignature> sky_signature;
    ComPtr<ID3D12PipelineState> sky_pipeline,hud_triangles_pso,
                                  hud_lines_pso,hdr_sky_pipeline;
    ComPtr<ID3D12DescriptorHeap> sky_srv_heap;
    ComPtr<ID3D12Resource> sky_vertex,sky_index,sky_texture;
    ComPtr<ID3D12Resource> sky_camera,sky_tint,hud_camera;
    // Optional engine.render UI overlay (RGBA8 upload -> post-tone-map pass).
    std::vector<std::uint8_t> ui_cpu_rgba;
    NorthStarRenderUiHoverFx ui_hover_fx{};
    std::string ui_shader_hlsl; // VFS-owned project shader; empty = generic engine fallback.
    bool ui_upload_dirty=true;
    UINT ui_width=0,ui_height=0,ui_gpu_width=0,ui_gpu_height=0;
    ComPtr<ID3D12Resource> ui_texture,ui_upload;
    ComPtr<ID3D12DescriptorHeap> ui_srv_heap;
    ComPtr<ID3D12RootSignature> ui_signature;
    ComPtr<ID3D12PipelineState> ui_pso;
    D3D12_RESOURCE_STATES ui_texture_state=D3D12_RESOURCE_STATE_COPY_DEST;
    void set_ui_rgba(const std::uint8_t* pixels,UINT w,UINT h,UINT stride);
    void record_ui_pass(FramePassContext& context);
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
    void wait_for_fence(UINT64 value);
    void resize(UINT w,UINT h);
    void ensure_gbuffer();
    void ensure_light_buffers();
    void set_lights(NorthStarRenderLight const* data,UINT count);
    void ensure_material_buffers(std::size_t required_vertices,
                                 std::size_t required_batches);
    std::shared_ptr<MaterialGpuTexture> material_texture(
        NorthStarMaterialTexture const& image,bool is_srgb,
        std::vector<MaterialTextureUpload>& pending,UploadPages::Batch& uploads);
    void set_material_batches(NorthStarMaterialBatch const* batches,UINT count);
    ComPtr<ID3D12PipelineState> material_graph_pipeline(
        std::string const& prefix,unsigned blend_mode);
    void resolve_material_graph_pipelines();
    void set_material_batches_v2(NorthStarMaterialBatchV2 const* batches,UINT count);
    void set_triangles(const NorthStarRenderTriangleVertex* source,UINT count);
    void set_sky(NorthStarSky const* source);
    NorthStarRenderTimingStats timing_stats();
    NorthStarRenderMemoryStats memory_stats() const;
    void verify_readback(ID3D12Resource* pixels,UINT pitch,UINT mode);
    void set_editor_grid(NorthStarRenderEditorGridSettings const* settings);
    void draw_editor_grid(FramePassContext& context);
    void draw_translucent_materials();
    void draw(const NorthStarRenderFrame& frame);
    int pick_cube(std::uint32_t x,std::uint32_t y);
    void set_pick_triangles(const NorthStarRenderPickVertex*,std::uint32_t);
    void set_pick_cubes(const std::uint64_t*,std::uint32_t);
    std::uint64_t pick_entity(std::uint32_t,std::uint32_t);
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
