// NorthStarCpp - opt-in Vulkan CPU synchronization and upload diagnostics.
// Internal-only: no changes to provider ABI, Vk API contracts, or normal frame path.
#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>
namespace northstarcpp::render::vulkan {
enum class VulkanWaitKind : unsigned {
    frame_fence, image_acquire, texture_queue_idle, device_idle, count
};
enum class VulkanCounter : unsigned {
    texture_upload_bytes, texture_uploads, texture_cache_hits, texture_descriptor_rebuilds,
    skin_vertex_upload_bytes, skin_palette_upload_bytes, skin_buffer_grows,
    material_vertex_upload_bytes, material_buffer_grows, count
};
class VulkanFrameDiagnostics {
public:
    struct Sample {
        double waits_ms[static_cast<unsigned>(VulkanWaitKind::count)]{};
        std::uint64_t counters[static_cast<unsigned>(VulkanCounter::count)]{};
    };
    VulkanFrameDiagnostics();
    bool enabled() const noexcept { return target_ != 0; }
    void add(VulkanCounter key, std::uint64_t delta = 1) noexcept {
        if (enabled()) current_.counters[static_cast<unsigned>(key)] += delta;
    }
    template<class F>
    auto time_wait(VulkanWaitKind key, F&& operation) -> decltype(operation()) {
        if (!enabled()) return operation();
        auto const begin = std::chrono::steady_clock::now();
        try {
            auto result = operation();
            current_.waits_ms[static_cast<unsigned>(key)] +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - begin).count();
            return result;
        } catch (...) {
            current_.waits_ms[static_cast<unsigned>(key)] +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - begin).count();
            throw;
        }
    }
    void end_frame() noexcept;
    void report(std::FILE* out) const noexcept;
    std::size_t samples() const noexcept { return samples_.size(); }
    void configure_for_test(std::size_t frames);
private:
    std::size_t target_ = 0;
    Sample current_{};
    std::vector<Sample> samples_;
};
} // namespace northstarcpp::render::vulkan
