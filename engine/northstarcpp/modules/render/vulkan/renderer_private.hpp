#pragma once
// NorthStarCpp Vulkan Win32 backend, private to engine.render provider.
// Stable C ABI 5; Vulkan objects do not escape the module boundary.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <windows.h>
#include <vulkan/vulkan.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_postfx_api.h>
#include "../shared/postfx_look.hpp"
#include <northstarcpp/render_ui_api.h>
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_materials_api.h>
#include <northstarcpp/render_skinning_api.h>
#include <northstarcpp/render_surfaces_api.h>
#include <northstarcpp/render_lights_api.h>
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_device_api.h>
#include <northstarcpp/render_pick_api.h>
#include <northstarcpp/render_shader_reload_api.h>
#include "../shared/render_graph.hpp"
#include "../shared/gbuffer_semantics.hpp"
#include "../shared/temporal_history.hpp"
#include "../shared/tiled_light_cache.hpp"
#include "../shared/render_graph_vulkan_executor.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace northstarcpp::render::vulkan {
struct ColoredVertex {
    float clip[4];
    float color[4];
    float world[3];
    float surface=0.f;
};
struct MaterialClipVertex {
    float clip[4];
    float uv[2];
    float normal[3];
    float tint[4];
    float params[4];
    float world[3];
};
struct MaterialSource {
    std::vector<NorthStarMaterialVertex> vertices;
    std::array<float,4> tint{};
    std::array<std::vector<std::uint8_t>,5> textures;
    std::array<std::uint32_t,5> widths{},heights{};
    float roughness=1.f,metallic=0.f;
    std::uint32_t unlit=0;
    std::uint32_t blend_mode=0;
    float alpha_cutoff=0.5f;
    std::string variant_prefix;
};
struct SkinSource {
    MaterialSource material;
    std::vector<NorthStarSkinMaterialVertex> vertices;
    std::vector<float> palette; // Packed native row-major matrices.
};
struct SkinDraw {
    std::uint32_t palette_offset=0; // float index in the packed palette buffer
    std::uint32_t first=0,count=0;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkPipeline gbuffer_pipeline=VK_NULL_HANDLE;
    VkDescriptorSet textures=VK_NULL_HANDLE;
};
struct SkinComputePush {
    float camera[4]{};
    float angles[4]{};
    float projection[4]{};
    float tint[4]{};
    float params[4]{};
    std::uint32_t offsets[4]{};
};
static_assert(sizeof(MaterialClipVertex)==80);
static_assert(sizeof(NorthStarSkinMaterialVertex)==80);
static_assert(sizeof(SkinComputePush)==96);
struct MaterialTextureGpu {
    VkImage image=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    std::uint32_t width=0,height=0;
    VkFormat format=VK_FORMAT_UNDEFINED;
    std::vector<std::uint8_t> source_bytes; // guards against hash collisions
};
struct MaterialDraw {
    std::uint32_t first=0,count=0;
    VkDescriptorSet textures=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkPipeline gbuffer_pipeline=VK_NULL_HANDLE;
    std::uint32_t blend_mode=0;
};
struct Renderer {
    explicit Renderer(HWND window, bool software);
    ~Renderer() noexcept;
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    void draw(const NorthStarRenderFrame& frame);
    int pick_cube(std::uint32_t x,std::uint32_t y);
    void set_pick_triangles(const NorthStarRenderPickVertex*,std::uint32_t);
    void set_pick_cubes(const std::uint64_t*,std::uint32_t);
    std::uint64_t pick_entity(std::uint32_t,std::uint32_t);
    bool enqueue_async_pick(NorthStarRenderPickAsyncRequest const&);
    NorthStarRenderPickAsyncResult poll_async_pick(std::uint64_t);
    void invalidate_async_pick(std::uint64_t) noexcept;
    std::vector<ColoredVertex> build_pick_vertices(std::vector<std::uint64_t>&);
    void set_triangles(const NorthStarRenderTriangleVertex* vertices,std::uint32_t count);
    void set_material_batches(const NorthStarMaterialBatch* batches,
                              std::uint32_t count);
    void set_material_batches_v2(const NorthStarMaterialBatchV2* batches,
                                 std::uint32_t count);
    void set_skinned_material_batches(
        const NorthStarSkinnedMaterialBatch* batches,std::uint32_t count);
    void set_world_texture(const NorthStarSurfaceTexture& texture);
    void set_cube_surfaces(const std::uint32_t* ids,std::uint32_t count);
    void set_material_references(const char* const* refs,std::uint32_t count);
    void set_film_look(NorthStarRenderPostFxSettings const& settings);
    NorthStarRenderPostFxSettings get_film_look() const noexcept {return film_look_;}
    void queue_material_stage_spirv(const char* ref,std::uint32_t stage,
        const std::uint32_t* spirv,std::uint32_t words);
    void queue_material_fragment_spirv(const char* ref,
        const std::uint32_t* spirv,std::uint32_t words);
    bool shader_reload_status(const char* ref,NorthStarShaderReloadStatus& status) const;
    void set_lights(const NorthStarRenderLight* lights,std::uint32_t count);
    void set_sky(const NorthStarSky* source);
    void set_ui_hover_fx(const NorthStarRenderUiHoverFx* fx) noexcept {
        ui_hover_fx_=fx?*fx:NorthStarRenderUiHoverFx{};
    }
    void set_ui_fragment_spirv(const std::uint32_t* words,std::uint32_t count);
    void set_ui_rgba(const std::uint8_t* pixels,std::uint32_t width,
                     std::uint32_t height,std::uint32_t stride);
    bool timing(NorthStarRenderTimingStats& stats) const noexcept;
    bool device_info(NorthStarRenderDeviceInfo& result) const noexcept;
private:
    struct HotBuild;
    void shutdown() noexcept;
    void create_swapchain();
    void destroy_swapchain() noexcept;
    void create_pipeline();
    void destroy_pipeline() noexcept;
    void create_vertex_buffer();
    void destroy_vertex_buffer() noexcept;
    void create_material_resources();
    void ensure_material_vertex_capacity(VkDeviceSize required_bytes);
    void destroy_material_resources() noexcept;
    void create_material_pipeline();
    void pump_shader_reload();
    void stop_shader_reload() noexcept;
    HotBuild build_shader_override(const std::array<std::vector<std::uint32_t>,3>& code,
                                   std::uint32_t blend_mode);
    VkPipeline create_pipeline_for_material_fragment(VkShaderModule fragment,
                                                     std::uint32_t blend_mode,
                                                     bool deferred=false,
                                                     VkShaderModule vertex_override=VK_NULL_HANDLE);
    VkPipeline material_graph_pipeline(const std::string& prefix,
                                       std::uint32_t blend_mode,
                                       bool deferred=false);
    void reset_material_textures();
    void upload_material_textures();
    MaterialTextureGpu upload_material_texture(const MaterialSource& batch,
                                                std::uint32_t slot);
    void collect_material_vertices(const NorthStarRenderFrame& frame);
    void upload_light_buffer();
    void create_skin_resources();
    void ensure_skin_buffers();
    void destroy_skin_resources() noexcept;
    void create_world_resources();
    void destroy_world_resources() noexcept;
    void create_world_pipeline();
    void upload_skin_geometry();
    void record_skin_compute(VkCommandBuffer command,
                             const NorthStarRenderFrame& frame);
    void create_readback();
    void destroy_readback() noexcept;
    void create_depth();
    void create_deferred_resources();
    void destroy_deferred_resources() noexcept;
    void create_deferred_pipelines();
    void record_gbuffer(VkCommandBuffer command,const NorthStarRenderFrame& frame);
    void update_deferred_tiles(const NorthStarRenderFrame& frame);
    void record_deferred_resolve(VkCommandBuffer command,std::uint32_t image);
    void record_deferred_forward(VkCommandBuffer command,std::uint32_t image);
    void destroy_depth() noexcept;
    void record_opaque(VkCommandBuffer cmd,std::uint32_t image,
                       const NorthStarRenderFrame& frame);
    void ensure_ui_resources();
    void destroy_ui_resources() noexcept;
    void record_ui_overlay(VkCommandBuffer cmd,std::uint32_t image);
    void record_readback(VkCommandBuffer cmd,std::uint32_t image,
                         const NorthStarRenderFrame& frame);
    void validate_readback(const NorthStarRenderFrame& frame);
    std::uint32_t find_memory_type(std::uint32_t bits,
                                   VkMemoryPropertyFlags flags) const;
    void collect_vertices(const NorthStarRenderFrame& frame);
    HWND hwnd_=nullptr;
    VkInstance instance_=VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_=VK_NULL_HANDLE;
    std::atomic<std::uint32_t> validation_errors_{0};
    VkSurfaceKHR surface_=VK_NULL_HANDLE;
    VkPhysicalDevice physical_=VK_NULL_HANDLE;
    VkDevice device_=VK_NULL_HANDLE;
    VkQueue queue_=VK_NULL_HANDLE;
    std::uint32_t family_=0;
    VkSwapchainKHR swapchain_=VK_NULL_HANDLE;
    VkFormat format_=VK_FORMAT_UNDEFINED;
    VkFormat depth_format_=VK_FORMAT_UNDEFINED;
    // Real offscreen Vulkan MRT targets. Provider-private, opt-in until
    // authored YDD/YDR and skinned material paths have deferred PSOs.
    bool deferred_enabled_=false;
    bool ssao_enabled_=false;
    bool gbuffer_v2_=false;
    NorthStarRenderPostFxSettings film_look_=northstarcpp::render::neutral_postfx();
    std::uint32_t film_frame_=0;
    std::array<VkFormat,4> gbuffer_formats_{{
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R8G8B8A8_UNORM}};
    std::array<VkImage,4> gbuffer_images_{};
    std::array<VkDeviceMemory,4> gbuffer_memories_{};
    std::array<VkImageView,4> gbuffer_views_{};
    VkSampler gbuffer_sampler_=VK_NULL_HANDLE;
    VkBuffer deferred_tile_buffer_=VK_NULL_HANDLE;
    VkDeviceMemory deferred_tile_memory_=VK_NULL_HANDLE;
    VkDeviceSize deferred_tile_bytes_=0;
    std::uint32_t deferred_tile_columns_=0,deferred_tile_rows_=0;
    northstarcpp::render::TileLightCache deferred_tile_cache_;
    VkDescriptorSetLayout gbuffer_layout_=VK_NULL_HANDLE;
    VkDescriptorPool gbuffer_pool_=VK_NULL_HANDLE;
    VkDescriptorSet gbuffer_set_=VK_NULL_HANDLE;
    VkPipeline gbuffer_pipeline_=VK_NULL_HANDLE;
    VkPipeline deferred_resolve_pipeline_=VK_NULL_HANDLE;
    VkPipelineLayout deferred_resolve_layout_=VK_NULL_HANDLE;
    VkImage depth_image_=VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_=VK_NULL_HANDLE;
    VkImageView depth_view_=VK_NULL_HANDLE;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
    VkCommandPool command_pool_=VK_NULL_HANDLE;
    VkCommandBuffer command_=VK_NULL_HANDLE;
    VkSemaphore available_=VK_NULL_HANDLE;
    // Present semaphore is owned by the acquired image, not by the CPU
    // frame. The image cannot be reacquired before the previous present wait.
    std::vector<VkSemaphore> present_ready_;
    VkFence completed_=VK_NULL_HANDLE;
    VkQueryPool timing_queries_=VK_NULL_HANDLE;
    double timestamp_period_ns_=0.0;
    std::uint32_t timestamp_valid_bits_=0;
    NorthStarRenderTimingStats last_timing_{};
    bool timing_pending_=false;
    void collect_completed_gpu_timing();
    northstarcpp::render::TemporalHistoryTracker temporal_tracker_;
    northstarcpp::render::TemporalFrameState temporal_frame_{};
    VkShaderModule vertex_shader_=VK_NULL_HANDLE;
    VkShaderModule fragment_shader_=VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_=VK_NULL_HANDLE;
    VkPipeline pipeline_=VK_NULL_HANDLE;
    VkPipeline pick_pipeline_=VK_NULL_HANDLE;
    VkShaderModule material_vertex_shader_=VK_NULL_HANDLE;
    VkShaderModule material_fragment_shader_=VK_NULL_HANDLE;
    VkPipelineLayout material_pipeline_layout_=VK_NULL_HANDLE;
    VkPipeline material_pipeline_=VK_NULL_HANDLE;
    VkPipeline material_gbuffer_pipeline_=VK_NULL_HANDLE;
    std::unordered_map<std::string,VkPipeline> material_variant_pipelines_;
    struct HotMaterial {
        std::array<std::vector<std::uint32_t>,3> stages;
        VkPipeline installed_forward=VK_NULL_HANDLE;
        VkPipeline installed_gbuffer=VK_NULL_HANDLE;
        std::uint64_t revision=0,installed_revision=0;
        std::uint32_t blend_mode=0,state=NS_SHADER_RELOAD_UNKNOWN;
        std::string error;
    };
    struct HotBuild {
        VkPipeline forward=VK_NULL_HANDLE;
        VkPipeline gbuffer=VK_NULL_HANDLE;
        std::string error;
    };
    std::vector<std::string> material_references_;
    std::unordered_map<std::string,HotMaterial> hot_materials_;
    std::deque<std::string> hot_pending_;
    std::future<HotBuild> hot_worker_;
    std::string hot_build_key_;
    std::uint64_t hot_build_revision_=0,hot_sequence_=0;
    VkDescriptorSetLayout material_set_layout_=VK_NULL_HANDLE;
    VkDescriptorPool material_descriptor_pool_=VK_NULL_HANDLE;
    VkSampler material_sampler_=VK_NULL_HANDLE;
    VkBuffer material_vertices_buffer_=VK_NULL_HANDLE;
    VkDeviceMemory material_vertices_memory_=VK_NULL_HANDLE;
    VkBuffer lights_buffer_=VK_NULL_HANDLE;
    VkDeviceMemory lights_memory_=VK_NULL_HANDLE;
    std::vector<NorthStarRenderLight> dynamic_lights_;
    bool lights_dirty_=true;
    std::unordered_map<std::uint64_t,MaterialTextureGpu> material_texture_cache_;
    std::vector<MaterialSource> material_sources_;
    std::vector<MaterialClipVertex> material_frame_vertices_;
    std::vector<MaterialDraw> material_draws_;
    std::vector<VkDescriptorSet> material_sets_;
    std::vector<SkinSource> skinned_sources_;
    std::vector<SkinDraw> skin_draws_;
    struct SkinBuffer {
        VkBuffer handle=VK_NULL_HANDLE;
        VkDeviceMemory memory=VK_NULL_HANDLE;
        VkDeviceSize capacity=0;
    };
    void destroy_skin_buffer(SkinBuffer& buffer) noexcept;
    SkinBuffer skin_input_,skin_output_,skin_palette_;
    VkDeviceSize skin_vertex_count_=0,skin_palette_floats_=0;
    VkDescriptorSetLayout skin_set_layout_=VK_NULL_HANDLE;
    VkDescriptorPool skin_descriptor_pool_=VK_NULL_HANDLE;
    VkDescriptorSet skin_set_=VK_NULL_HANDLE;
    VkPipelineLayout skin_pipeline_layout_=VK_NULL_HANDLE;
    VkShaderModule skin_shader_=VK_NULL_HANDLE;
    VkPipeline skin_pipeline_=VK_NULL_HANDLE;
    std::array<MaterialTextureGpu,9> world_textures_{};
    std::array<bool,9> world_texture_resident_{};
    std::vector<std::uint32_t> world_surface_ids_;
    VkDescriptorSetLayout world_set_layout_=VK_NULL_HANDLE;
    VkDescriptorPool world_descriptor_pool_=VK_NULL_HANDLE;
    VkDescriptorSet world_set_=VK_NULL_HANDLE;
    VkSampler world_sampler_=VK_NULL_HANDLE;
    VkShaderModule world_vs_=VK_NULL_HANDLE,world_fs_=VK_NULL_HANDLE;
    VkPipelineLayout world_pipeline_layout_=VK_NULL_HANDLE;
    VkPipeline world_pipeline_=VK_NULL_HANDLE;
    bool material_textures_dirty_=true;
    // Full-detail native vehicles are never downsampled to satisfy a GPU cap.
    // MaterialClipVertex is 80 bytes; 8M input vertices need up to 640 MiB.
    static constexpr VkDeviceSize max_material_vertex_bytes_=
        VkDeviceSize(NORTHSTARCPP_RENDER_MATERIAL_MAX_VERTICES)*sizeof(MaterialClipVertex);
    VkDeviceSize material_vertex_capacity_bytes_=0;
    VkBuffer vertices_buffer_=VK_NULL_HANDLE;
    VkDeviceMemory vertices_memory_=VK_NULL_HANDLE;
    VkBuffer readback_buffer_=VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_=VK_NULL_HANDLE;
    VkDeviceSize readback_capacity_=0;
    static constexpr VkDeviceSize max_vertex_bytes_=16ull*1024*1024;
    std::vector<NorthStarRenderTriangleVertex> scene_;
    std::vector<ColoredVertex> frame_vertices_;
    struct PickCubeRange {std::uint32_t first=0,count=0;};
    std::vector<PickCubeRange> pick_cube_ranges_;
    std::vector<NorthStarRenderPickVertex> pick_world_vertices_;
    std::vector<std::uint64_t> pick_cube_ids_;
    NorthStarRenderFrame pick_camera_{};
    bool pick_entity_mode_=false;
    std::uint64_t last_picked_entity_=0;
    struct AsyncPickSlot {
        bool active=false;
        std::uint64_t request_id=0,scene_generation=0;
        std::uint32_t width=0,height=0;
        VkFence fence=VK_NULL_HANDLE;
        VkCommandPool pool=VK_NULL_HANDLE;
        VkCommandBuffer command=VK_NULL_HANDLE;
        VkBuffer vertices=VK_NULL_HANDLE,readback=VK_NULL_HANDLE;
        VkDeviceMemory vertices_memory=VK_NULL_HANDLE;
        VkDeviceMemory readback_memory=VK_NULL_HANDLE;
        VkDeviceSize vertices_capacity=0;
        VkImage color=VK_NULL_HANDLE,depth=VK_NULL_HANDLE;
        VkImageView color_view=VK_NULL_HANDLE,depth_view=VK_NULL_HANDLE;
        VkDeviceMemory color_memory=VK_NULL_HANDLE,depth_memory=VK_NULL_HANDLE;
        std::vector<std::uint64_t> tokens;
    };
    std::array<AsyncPickSlot,3> async_pick_slots_{};
    std::uint64_t minimum_pick_generation_=0;
    void destroy_async_pick() noexcept;
    std::uint32_t render_vertices_=0;
    std::array<float,4> sky_clear_{0.037f,0.055f,0.09f,1.f};
    // Native Vulkan RGBA8 engine.ui compositor, independent of world materials.
    std::vector<std::uint8_t> ui_cpu_rgba_;
    NorthStarRenderUiHoverFx ui_hover_fx_{};
    std::vector<std::uint32_t> ui_fragment_spirv_; // Project shader override.
    bool ui_upload_dirty_=true;
    std::uint32_t ui_width_=0,ui_height_=0,ui_gpu_width_=0,ui_gpu_height_=0;
    VkImage ui_image_=VK_NULL_HANDLE;
    VkDeviceMemory ui_image_memory_=VK_NULL_HANDLE;
    VkImageView ui_view_=VK_NULL_HANDLE;
    VkBuffer ui_staging_=VK_NULL_HANDLE;
    VkDeviceMemory ui_staging_memory_=VK_NULL_HANDLE;
    VkImageLayout ui_image_layout_=VK_IMAGE_LAYOUT_UNDEFINED;
    VkSampler ui_sampler_=VK_NULL_HANDLE;
    VkDescriptorSetLayout ui_set_layout_=VK_NULL_HANDLE;
    VkDescriptorPool ui_descriptor_pool_=VK_NULL_HANDLE;
    VkDescriptorSet ui_descriptor_set_=VK_NULL_HANDLE;
    VkPipelineLayout ui_pipeline_layout_=VK_NULL_HANDLE;
    VkPipeline ui_pipeline_=VK_NULL_HANDLE;
    northstarcpp::render::RenderGraphPlan graph_plan_;
    northstarcpp::render::RenderGraphPlan deferred_graph_plan_;
    bool swapchain_transfer_src_=false;
    std::vector<bool> swapchain_seen_;
};
}
