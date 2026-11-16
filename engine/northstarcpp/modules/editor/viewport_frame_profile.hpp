// Rolling CPU submission telemetry for the native Qt GPU Viewport.
// This measures host wall-clock time, NOT Vulkan/D3D12 GPU execution time.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace northstarcpp::editor {
struct ViewportFrameQuantiles {
    std::size_t samples=0;
    double p50_ms=0.0;
    double p95_ms=0.0;
    double p99_ms=0.0;
};
class ViewportFrameProfile {
public:
    static constexpr std::size_t capacity=1024;
    void clear() noexcept {count_=0;cursor_=0;}
    void sample(double milliseconds) noexcept {
        if(!std::isfinite(milliseconds) || milliseconds<0.0)return;
        samples_[cursor_]=milliseconds;
        cursor_=(cursor_+1)%capacity;
        if(count_<capacity)++count_;
    }
    std::size_t count() const noexcept {return count_;}
    ViewportFrameQuantiles summarize() const {
        ViewportFrameQuantiles out;
        out.samples=count_;
        if(!count_)return out;
        std::vector<double> ordered(samples_.begin(),samples_.begin()+count_);
        std::sort(ordered.begin(),ordered.end());
        auto percentile=[&ordered](double fraction) {
            double const position=fraction*double(ordered.size()-1);
            auto const low=static_cast<std::size_t>(position);
            auto const high=std::min(low+1,ordered.size()-1);
            return ordered[low]+(ordered[high]-ordered[low])*
                (position-double(low));
        };
        out.p50_ms=percentile(.50);
        out.p95_ms=percentile(.95);
        out.p99_ms=percentile(.99);
        return out;
    }
private:
    std::array<double,capacity> samples_{};
    std::size_t count_=0;
    std::size_t cursor_=0;
};
} // namespace northstarcpp::editor
