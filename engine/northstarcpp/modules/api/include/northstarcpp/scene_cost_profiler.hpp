// NorthStarCpp scene-cost telemetry. Only explicitly sampled values are reported.
// Missing GPU timings and backend VRAM samples remain unknown, not zero.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <vector>

namespace northstarcpp {
struct SceneCostSample {
    std::uint64_t sequence=0;
    std::uint64_t cpu_us=0;
    std::optional<std::uint64_t> gpu_us;
    std::optional<std::uint64_t> gpu_bytes;
};
struct SceneCostQuantiles {
    std::size_t samples=0;
    std::uint64_t p50=0,p95=0,p99=0,max=0;
};
struct SceneCostSummary {
    SceneCostQuantiles cpu_us, gpu_us, gpu_bytes;
    std::uint64_t newest_sequence=0;
};

class SceneCostProfiler {
public:
    explicit SceneCostProfiler(std::size_t history=1024)
        : ring_(history) {
        if(history==0 || history>65536)
            throw std::invalid_argument("Scene profiler history out of range");
    }

    // SceneWorld calls this once per update, without per-frame allocations.
    std::uint64_t record_cpu(std::uint64_t cpu_us) noexcept {
        auto& sample=ring_[next_];
        sample={++sequence_,cpu_us,std::nullopt,std::nullopt};
        next_=(next_+1)%ring_.size();
        count_=std::min(count_+1,ring_.size());
        return sequence_;
    }

    // Renderer supplies actual GPU query/fence-derived time and allocation
    // telemetry for the SAME frame. Late or expired samples are ignored.
    bool record_gpu(std::uint64_t sequence,std::uint64_t gpu_us,
                    std::optional<std::uint64_t> gpu_bytes=std::nullopt) noexcept {
        if(sequence==0 || sequence>sequence_ || sequence_ - sequence >= count_)
            return false;
        auto const index=(next_+ring_.size()-1-static_cast<std::size_t>(sequence_-sequence))
                         %ring_.size();
        auto& sample=ring_[index];
        if(sample.sequence!=sequence)return false;
        sample.gpu_us=gpu_us;
        sample.gpu_bytes=gpu_bytes;
        return true;
    }

    SceneCostSummary summary() const {
        std::vector<std::uint64_t> cpu,gpu,bytes;
        cpu.reserve(count_);gpu.reserve(count_);bytes.reserve(count_);
        for(std::size_t n=0;n<count_;++n) {
            auto const& sample=at(n);
            cpu.push_back(sample.cpu_us);
            if(sample.gpu_us)gpu.push_back(*sample.gpu_us);
            if(sample.gpu_bytes)bytes.push_back(*sample.gpu_bytes);
        }
        return {quantiles(cpu),quantiles(gpu),quantiles(bytes),sequence_};
    }

    std::size_t sample_count() const noexcept {return count_;}
    std::uint64_t newest_sequence() const noexcept {return sequence_;}
    void write_csv(std::ostream& dst) const {
        dst<<"frame,cpu_us,gpu_us,gpu_bytes\n";
        for(std::size_t n=0;n<count_;++n) {
            auto const& s=at(n);
            dst<<s.sequence<<','<<s.cpu_us<<',';
            if(s.gpu_us)dst<<*s.gpu_us;
            dst<<',';
            if(s.gpu_bytes)dst<<*s.gpu_bytes;
            dst<<'\n';
        }
    }
private:
    SceneCostSample const& at(std::size_t chronological) const noexcept {
        return ring_[(next_+ring_.size()-count_+chronological)%ring_.size()];
    }
    static SceneCostQuantiles quantiles(std::vector<std::uint64_t>& values) {
        if(values.empty())return {};
        std::sort(values.begin(),values.end());
        auto const rank=[&](std::size_t numerator)->std::uint64_t {
            // Nearest-rank percentile: ceil(p*N) - 1, no FP drift.
            auto const idx=(values.size()*numerator+99)/100-1;
            return values[std::min(idx,values.size()-1)];
        };
        return {values.size(),rank(50),rank(95),rank(99),values.back()};
    }
    std::vector<SceneCostSample> ring_;
    std::size_t next_=0,count_=0;
    std::uint64_t sequence_=0;
};
} // namespace northstarcpp
