// NorthStarCpp - opt-in CPU frame timeline, independent of backend and ABI.
// This file is an internal utility; it does not change engine.* provider tables.
#pragma once

#include "frame_memory_counters.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace northstarcpp::diagnostics {

enum class FrameStage : std::size_t {
    scripting, animation, physics, scene_streaming, rendering, asset_streaming, count
};

inline constexpr std::array<char const*, static_cast<std::size_t>(FrameStage::count)>
    frame_stage_names = {"scripting", "animation", "physics", "scene_streaming",
                         "rendering", "asset_streaming"};

using FrameMemory = std::array<NorthStarFrameMemoryCounters, frame_stage_names.size()>;

struct FrameSample {
    // Unclamped interval between frame timer callbacks. Includes waiting and
    // presentation from the previous frame; not a GPU timestamp.
    double interval_ms = 0;
    // CPU wall time spent inside App::frame, including renderer submission/Present.
    double wall_ms = 0;
    // Optional hardware GPU frame timestamp; 0 is not considered a sample.
    double gpu_frame_ms = 0;
    bool gpu_valid = false;
    std::array<double, static_cast<std::size_t>(FrameStage::count)> stage_ms{};
};

struct FrameStatistics {
    double p50 = 0;
    double p95 = 0;
    double p99 = 0;
    double maximum = 0;
};

inline FrameStatistics summarize_frames(std::vector<double> values)
{
    if (values.empty())
        return {};
    std::sort(values.begin(), values.end());
    auto percentile = [&](double portion) {
        double index = (values.size() - 1) * portion;
        auto lower = static_cast<std::size_t>(index);
        auto upper = std::min(lower + 1, values.size() - 1);
        return values[lower] + (values[upper] - values[lower]) * (index - lower);
    };
    return {percentile(.50), percentile(.95), percentile(.99), values.back()};
}

class FrameProfiler {
public:
    explicit FrameProfiler(std::size_t frames) : target_(frames)
    {
        if (frames < 1 || frames > 1000000)
            throw std::invalid_argument("Invalid frame profiler sample count");
        samples_.reserve(frames);
    }

    // QA-only partial report when the application exits before its frame goal.
    // Production mode emits no partial report or additional allocations.
    ~FrameProfiler() noexcept
    {
        auto const* enabled = std::getenv("NORTHSTARCPP_PROFILE_PARTIAL");
        if (!enabled || enabled[0] != '1' || samples_.empty() || !active())
            return;
        std::fprintf(stderr, "[WARN] [engine.profile] incomplete_frames=%zu target=%zu\n",
                     samples_.size(), target_);
        try { report(stderr); } catch (...) {}
    }

    static std::size_t requested_frames()
    {
        auto const* text = std::getenv("NORTHSTARCPP_PROFILE_FRAMES");
        if (!text || !*text)
            return 0; // No allocations or extra clock samples by default.
        std::size_t result = 0;
        auto const length = std::char_traits<char>::length(text);
        auto const parsed = std::from_chars(text, text + length, result);
        if (parsed.ec != std::errc{} || parsed.ptr != text + length ||
            result < 1000 || result > 1000000)
            throw std::invalid_argument(
                "NORTHSTARCPP_PROFILE_FRAMES must be an integer in [1000, 1000000]");
        return result;
    }

    bool active() const noexcept { return samples_.size() < target_; }
    std::size_t collected() const noexcept { return samples_.size(); }

    void record(FrameSample sample, FrameMemory const& memory = {})
    {
        if (!active())
            return;
        samples_.push_back(sample);
        for (std::size_t i=0; i<memory.size(); ++i) {
            auto const& delta=memory[i];
            auto& total=memory_[i];
            total.allocations+=delta.allocations;
            total.allocation_bytes+=delta.allocation_bytes;
            total.geometry_copy_bytes+=delta.geometry_copy_bytes;
            total.texture_copy_bytes+=delta.texture_copy_bytes;
            total.palette_copy_bytes+=delta.palette_copy_bytes;
            total.device_allocations+=delta.device_allocations;
            total.device_allocation_bytes+=delta.device_allocation_bytes;
        }
        if (!active())
            report(stderr);
    }

    FrameStatistics statistics_interval() const { return statistics([](FrameSample const& s) { return s.interval_ms; }); }
    FrameStatistics statistics_wall() const { return statistics([](FrameSample const& s) { return s.wall_ms; }); }
    std::size_t gpu_samples() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(samples_.begin(), samples_.end(),
            [](FrameSample const& s) { return s.gpu_valid; }));
    }
    FrameStatistics statistics_gpu() const
    {
        std::vector<double> values;
        values.reserve(samples_.size());
        for (auto const& sample : samples_)
            if (sample.gpu_valid) values.push_back(sample.gpu_frame_ms);
        return summarize_frames(std::move(values));
    }
    FrameStatistics statistics_stage(FrameStage stage) const
    {
        return statistics([stage](FrameSample const& s) {
            return s.stage_ms[static_cast<std::size_t>(stage)];
        });
    }

    NorthStarFrameMemoryCounters memory_stage(FrameStage stage) const noexcept
    {
        return memory_[static_cast<std::size_t>(stage)];
    }

    void report(std::FILE* out) const
    {
        // CPU and GPU clocks are independent. Hardware timing is optional.
        auto const valid_gpu = gpu_samples();
        std::fprintf(out, "{\"event\":\"northstarcpp.frame_profile\","
            "\"frames\":%zu,\"units\":\"ms\","
            "\"gpu_timestamps\":%s,\"gpu_samples\":%zu,\"metrics\":{",
            samples_.size(), valid_gpu ? "true" : "false", valid_gpu);
        auto print = [&](char const* name, FrameStatistics const& value, bool comma) {
            std::fprintf(out,
                "%s\"%s\":{\"p50\":%.5f,\"p95\":%.5f,\"p99\":%.5f,\"max\":%.5f}",
                comma ? "," : "", name, value.p50, value.p95, value.p99, value.maximum);
        };
        print("raw_frame_interval", statistics_interval(), false);
        print("frame_cpu_wall", statistics_wall(), true);
        print("gpu_frame", statistics_gpu(), true);
        for (std::size_t i = 0; i < frame_stage_names.size(); ++i)
            print(frame_stage_names[i], statistics_stage(static_cast<FrameStage>(i)), true);
        std::fputs("},\"memory\":{\"coverage\":\"instrumented_buffers\",\"stages\":{",out);
        for (std::size_t i=0; i<memory_.size(); ++i) {
            auto const& value=memory_[i];
            std::fprintf(out,
                "%s\"%s\":{\"allocations\":%llu,\"allocation_bytes\":%llu,"
                "\"geometry_copy_bytes\":%llu,\"texture_copy_bytes\":%llu,"
                "\"palette_copy_bytes\":%llu,\"device_allocations\":%llu,"
                "\"device_allocation_bytes\":%llu}",i?",":"",frame_stage_names[i],
                static_cast<unsigned long long>(value.allocations),
                static_cast<unsigned long long>(value.allocation_bytes),
                static_cast<unsigned long long>(value.geometry_copy_bytes),
                static_cast<unsigned long long>(value.texture_copy_bytes),
                static_cast<unsigned long long>(value.palette_copy_bytes),
                static_cast<unsigned long long>(value.device_allocations),
                static_cast<unsigned long long>(value.device_allocation_bytes));
        }
        std::fputs("}}}\n",out);
        std::fflush(out);
    }

private:
    template<class F>
    FrameStatistics statistics(F&& field) const
    {
        std::vector<double> values;
        values.reserve(samples_.size());
        for (auto const& sample : samples_)
            values.push_back(field(sample));
        return summarize_frames(std::move(values));
    }
    std::size_t target_;
    std::vector<FrameSample> samples_;
    FrameMemory memory_{};
};

// Scope guard also records frames that return early from the editor branch.
// Records are suppressed if the target sample count has already been reached.
class FrameProfileScope {
public:
    using Clock = std::chrono::steady_clock;

    FrameProfileScope(FrameProfiler* owner, double unclamped_seconds)
        : profiler_(owner && owner->active() ? owner : nullptr)
    {
        if (profiler_) {
            sample_.interval_ms = unclamped_seconds * 1000.0;
            started_ = Clock::now();
            previous_=current_;
            current_=this;
        }
    }
    FrameProfileScope(FrameProfileScope const&) = delete;
    FrameProfileScope& operator=(FrameProfileScope const&) = delete;

    ~FrameProfileScope() noexcept
    {
        if (!profiler_)
            return;
        current_=previous_;
        sample_.wall_ms = milliseconds(started_, Clock::now());
        try {
            profiler_->record(sample_, memory_);
        } catch (...) {
            // Profiling cannot cause a gameplay failure on allocation/IO errors.
        }
    }

    // Record only valid hardware timestamps in explicit GPU profiling mode.
    void record_gpu_timing(double milliseconds) noexcept
    {
        if (!profiler_ || !std::isfinite(milliseconds) ||
            milliseconds <= 0.0 || milliseconds >= 10000.0) return;
        sample_.gpu_frame_ms = milliseconds;
        sample_.gpu_valid = true;
    }

    template<class F>
    void measure(FrameStage stage, F&& callback)
    {
        if (!profiler_) {
            callback();
            return;
        }
        frame_memory::Scope memory_scope(&memory_[static_cast<std::size_t>(stage)]);
        auto const started = Clock::now();
        try {
            callback();
        } catch (...) {
            account(stage, started);
            throw;
        }
        account(stage, started);
    }

    template<class F>
    static void measure_current(FrameStage stage, F&& callback)
    {
        if (current_) current_->measure(stage, std::forward<F>(callback));
        else callback();
    }

    void account(FrameStage stage, Clock::time_point started)
    {
        if (profiler_)
            sample_.stage_ms[static_cast<std::size_t>(stage)] +=
                milliseconds(started, Clock::now());
    }

    bool active() const noexcept { return profiler_ != nullptr; }

private:
    static double milliseconds(Clock::time_point a, Clock::time_point b)
    {
        return std::chrono::duration<double, std::milli>(b - a).count();
    }
    FrameProfiler* profiler_ = nullptr;
    inline static thread_local FrameProfileScope* current_=nullptr;
    FrameProfileScope* previous_=nullptr;
    FrameSample sample_;
    FrameMemory memory_{};
    Clock::time_point started_{};
};

} // namespace northstarcpp::diagnostics
