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
#include <northstarcpp/render_triangles_api.h>
#include <northstarcpp/render_timing_api.h>
#include <northstarcpp/render_device_api.h>
#include "../shared/render_graph.hpp"
#include "../shared/render_graph_vulkan_executor.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace northstarcpp::render::vulkan {
struct ColoredVertex {
    float clip[4];
    float color[4];
};
struct Renderer {
    explicit Renderer(HWND window, bool software);
    ~Renderer() noexcept;
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    void draw(const NorthStarRenderFrame& frame);
    void set_triangles(const NorthStarRenderTriangleVertex* vertices,std::uint32_t count);
    void set_sky(const NorthStarSky* source);
    bool timing(NorthStarRenderTimingStats& stats) const noexcept;
    bool device_info(NorthStarRenderDeviceInfo& result) const noexcept;
private:
    void shutdown() noexcept;
    void create_swapchain();
    void destroy_swapchain() noexcept;
    void create_pipeline();
    void destroy_pipeline() noexcept;
    void create_vertex_buffer();
    void destroy_vertex_buffer() noexcept;
    void create_readback();
    void destroy_readback() noexcept;
    void record_opaque(VkCommandBuffer cmd,std::uint32_t image,
                       const NorthStarRenderFrame& frame);
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
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
    VkCommandPool command_pool_=VK_NULL_HANDLE;
    VkCommandBuffer command_=VK_NULL_HANDLE;
    VkSemaphore available_=VK_NULL_HANDLE;
    VkSemaphore ready_=VK_NULL_HANDLE;
    VkFence completed_=VK_NULL_HANDLE;
    VkQueryPool timing_queries_=VK_NULL_HANDLE;
    double timestamp_period_ns_=0.0;
    std::uint32_t timestamp_valid_bits_=0;
    NorthStarRenderTimingStats last_timing_{};
    VkShaderModule vertex_shader_=VK_NULL_HANDLE;
    VkShaderModule fragment_shader_=VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_=VK_NULL_HANDLE;
    VkPipeline pipeline_=VK_NULL_HANDLE;
    VkBuffer vertices_buffer_=VK_NULL_HANDLE;
    VkDeviceMemory vertices_memory_=VK_NULL_HANDLE;
    VkBuffer readback_buffer_=VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_=VK_NULL_HANDLE;
    VkDeviceSize readback_capacity_=0;
    static constexpr VkDeviceSize max_vertex_bytes_=16ull*1024*1024;
    std::vector<NorthStarRenderTriangleVertex> scene_;
    std::vector<ColoredVertex> frame_vertices_;
    std::uint32_t render_vertices_=0;
    std::array<float,4> sky_clear_{0.037f,0.055f,0.09f,1.f};
    northstarcpp::render::RenderGraphPlan graph_plan_;
    bool swapchain_transfer_src_=false;
    std::vector<bool> swapchain_seen_;
};
}
