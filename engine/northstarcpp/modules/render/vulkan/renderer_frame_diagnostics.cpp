#include "renderer_frame_diagnostics.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <string_view>
namespace northstarcpp::render::vulkan {
namespace {
constexpr const char* wait_names[]{
    "frame_fence","image_acquire","texture_queue_idle","device_idle"
};
constexpr const char* counter_names[]{
    "texture_upload_bytes","texture_uploads","texture_cache_hits",
    "texture_descriptor_rebuilds","skin_vertex_upload_bytes",
    "skin_palette_upload_bytes","skin_buffer_grows",
    "material_vertex_upload_bytes","material_buffer_grows"
};
struct Quantiles { double p50=0.0,p95=0.0,p99=0.0,max=0.0; };
Quantiles summarize(std::vector<double>& v) {
    if (v.empty()) return {};
    std::sort(v.begin(), v.end());
    auto pct = [&](double p) {
        auto index = (v.size()-1) * p;
        auto lower = static_cast<std::size_t>(index);
        auto upper = std::min(lower+1, v.size()-1);
        return v[lower]+(v[upper]-v[lower])*(index-lower);
    };
    return {pct(.50),pct(.95),pct(.99),v.back()};
}
} // namespace
VulkanFrameDiagnostics::VulkanFrameDiagnostics() {
    auto const* env=std::getenv("NORTHSTARCPP_VULKAN_PROFILE_FRAMES");
    if (!env) return;
    std::size_t requested=0;
    auto const length=std::char_traits<char>::length(env);
    auto const parsed=std::from_chars(env,env+length,requested);
    if (parsed.ec!=std::errc{} || parsed.ptr!=env+length ||
        requested<100 || requested>10000) return;
    configure_for_test(requested);
}
void VulkanFrameDiagnostics::configure_for_test(std::size_t frames) {
    target_=frames;
    samples_.clear();
    samples_.reserve(frames);
    current_={};
}
void VulkanFrameDiagnostics::end_frame() noexcept {
    if (!enabled() || samples_.size()>=target_) return;
    // Runtime allocations and refreshes are counted even if they occurred
    // in the upload API before Renderer::draw was invoked.
    try {
        samples_.push_back(current_);
        current_={};
        if (samples_.size()==target_) report(stderr);
    } catch (...) {} // Instrumentation must never crash the GPU provider.
}
void VulkanFrameDiagnostics::report(std::FILE* out) const noexcept {
    if (!out || samples_.empty()) return;
    std::fprintf(out,
        "{\"event\":\"northstarcpp.vulkan_frame_diagnostics\",\"frames\":%zu,"
        "\"timings_ms\":{",samples_.size());
    for(unsigned key=0;key<static_cast<unsigned>(VulkanWaitKind::count);++key) {
        std::vector<double> v;
        try {
            v.reserve(samples_.size());
            for(auto const& sample:samples_)v.push_back(sample.waits_ms[key]);
        }catch(...){return;}
        auto const q=summarize(v);
        auto const total=std::accumulate(v.begin(),v.end(),0.0);
        // 0 ms frames are deliberately included; no wait means no stall.
        std::fprintf(out,
            "%s\"%s\":{\"p50\":%.6f,\"p95\":%.6f,\"p99\":%.6f,"
            "\"max\":%.6f,\"total\":%.6f}",
            key?",":"",wait_names[key],q.p50,q.p95,q.p99,q.max,total);
    }
    std::fputs("},\"counters\":{",out);
    for(unsigned key=0;key<static_cast<unsigned>(VulkanCounter::count);++key) {
        std::uint64_t sum=0;
        for(auto const& sample:samples_)sum+=sample.counters[key];
        std::fprintf(out,"%s\"%s\":%llu",key?",":"",counter_names[key],
            static_cast<unsigned long long>(sum));
    }
    std::fputs("}}\n",out);
    std::fflush(out);
}
} // namespace northstarcpp::render::vulkan
