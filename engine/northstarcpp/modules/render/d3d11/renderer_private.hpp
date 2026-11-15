#pragma once
// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/geometry.hpp>
#include <northstarcpp/core/math_standalone.h>
#include <northstarcpp/render_plugin.h>
#include <ui_quad.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_surfaces_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_skinning_api.h>
#include "../shared/material_hlsl.hpp"
#include "../shared/material_variants.hpp"
#include <northstarcpp/render_character_api.h>
#include <northstarcpp/scene.hpp>
#include "module_provider.hpp"
#include "shader_compiler.hpp"
#include "renderer_geometry.hpp"

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
#include <unordered_set>
#include <unordered_map>
#include <cstdint>


namespace northstarcpp::render::d3d11::detail {
using namespace northstarcpp::module_provider;
using northstarcpp::render::d3d11::geometry::Vertex;
using northstarcpp::render::d3d11::geometry::add_cube;
using northstarcpp::shaders::authored_material_shader;
using northstarcpp::render::d3d11::shader_compiler::compile;
using northstarcpp::render::d3d11::shader_compiler::compile_sky;
using northstarcpp::render::d3d11::shader_compiler::compile_character;
using northstarcpp::render::d3d11::shader_compiler::compile_authored_material;

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

inline void check(HRESULT result, char const* action)
{
    if (FAILED(result)) {
        char code[20]{};
        std::snprintf(code, sizeof(code), " (0x%08lx)", static_cast<unsigned long>(result));
        throw std::runtime_error(std::string(action) + code);
    }
}



struct SkyVertex {
    float x, y, z, u, v;
};
struct MaterialParams {
    float base_color[4];
    float factors[4]; // roughness, metallic, unlit, padding
    std::uint32_t present[4]; // bitmask, padding
};
static_assert(sizeof(MaterialParams)==48);
struct CachedTexture {
    Com<ID3D11Texture2D> gpu;
    Com<ID3D11ShaderResourceView> view;
};
struct MaterialPixelVariant {
    Com<ID3D11PixelShader> shader;
};
struct MaterialDraw {
    std::shared_ptr<MaterialPixelVariant> shader_variant;
    unsigned blend_mode=0;
    std::uint32_t first=0,count=0;
    MaterialParams params{};
    std::array<std::shared_ptr<CachedTexture>,5> maps{};
};
struct SkinnedMaterialDraw {
    MaterialDraw material;
    std::vector<float> palette; // Row-major float4x4 per joint.
};
static_assert(sizeof(NorthStarSkinMaterialVertex)==80,
              "Native skinning vertex layout changed");


static_assert(sizeof(SkyVertex) == sizeof(NorthStarSkyVertex));

constexpr unsigned grid_vertices = 84;
constexpr unsigned max_vertex_capacity =
    grid_vertices + 36 * northstarcpp::max_scene_objects;
constexpr unsigned min_vertex_capacity = 4096;

struct CharacterBatch {
    UINT start = 0, count = 0;
    float cutoff[4]{};
    Com<ID3D11ShaderResourceView> texture;
};

struct RendererState {
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
    Com<ID3D11VertexShader> character_vs;
    Com<ID3D11PixelShader> character_ps;
    Com<ID3D11InputLayout> character_layout;
    Com<ID3D11Buffer> character_material, character_vertices, character_indices;
    std::vector<CharacterBatch> character_parts;
    std::uint32_t character_vertex_count = 0;
    bool character_visible = false;
    Com<ID3D11VertexShader> authored_vs;
    Com<ID3D11VertexShader> skinned_vs;
    Com<ID3D11InputLayout> skinned_layout;
    Com<ID3D11Buffer> skinned_palette_buffer, skinned_vertex_buffer;
    std::size_t skinned_vertex_capacity=0;
    std::vector<NorthStarSkinMaterialVertex> skinned_vertices;
    std::vector<SkinnedMaterialDraw> skinned_draws;
    Com<ID3D11PixelShader> authored_ps;
    Com<ID3D11InputLayout> authored_layout;
    Com<ID3D11BlendState> material_alpha_blend;
    Com<ID3D11DepthStencilState> material_transparent_depth;
    Com<ID3D11Buffer> authored_material,authored_camera,material_gpu_vertices;
    std::unordered_map<std::string,std::shared_ptr<MaterialPixelVariant>>
        material_shader_cache;
    std::size_t material_gpu_capacity=0;
    std::vector<NorthStarMaterialVertex> material_cpu_vertices;
    std::vector<MaterialDraw> material_draws;
    std::unordered_map<std::uint64_t,std::shared_ptr<CachedTexture>> material_texture_cache;
    std::uint64_t material_texture_cache_bytes=0;
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
    std::array<Com<ID3D11Texture2D>,9> surface_textures;
    std::array<Com<ID3D11ShaderResourceView>,9> surface_views;
    std::vector<std::uint32_t> surface_ids;
    explicit RendererState(HWND target, bool software);
    HRESULT create_device(D3D_DRIVER_TYPE driver);
    void resize(unsigned new_width, unsigned new_height);
    void verify_frame(std::uint32_t mode);
    void draw_fps_overlay(NorthStarRenderFrame const& frame);
    void set_sky(NorthStarSky const* source);
    NorthStarRenderMemoryStats memory_stats() const;
    void draw_sky(northstarcpp::math::float3 const& direction,
                  northstarcpp::math::float3 const& up,
                  northstarcpp::math::float4x4 const& projection);
    void ensure_vertex_capacity(std::size_t required);
    void set_character(NorthStarCharacterPart const* parts, std::uint32_t count);
    void set_character_pose(NorthStarCharacterVertex const* data, std::uint32_t count);
    void draw_character();
    std::shared_ptr<CachedTexture> upload_material_texture(
        NorthStarMaterialTexture const& image,bool is_srgb);
    void set_material_batches(NorthStarMaterialBatch const* batches,std::uint32_t count);
    void set_material_batches_v2(NorthStarMaterialBatchV2 const* batches,
                                 std::uint32_t count);
    void set_skinned_material_batches(
        NorthStarSkinnedMaterialBatch const* batches,std::uint32_t count);
    void draw_skinned_materials();
    void draw_authored_materials();
    void set_triangles(NorthStarRenderTriangleVertex const* src,std::uint32_t count);
    void set_surface_texture(NorthStarSurfaceTexture const& image);
    void set_cube_surfaces(std::uint32_t const* ids,std::uint32_t count);
    void draw(NorthStarRenderFrame const& frame);
};
}
