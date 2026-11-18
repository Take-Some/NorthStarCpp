#include "../engine/northstarcpp/modules/shared/frame_profiler.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace northstarcpp::diagnostics;

void require(bool ok, char const* message)
{
    if (!ok) throw std::runtime_error(message);
}

int main()
{
    try {
        auto empty = summarize_frames({});
        require(empty.p50 == 0 && empty.maximum == 0, "Empty statistics");
        auto values = summarize_frames({1, 2, 3, 4, 5});
        require(values.p50 == 3 && values.p95 > 4.7 && values.p95 < 5.0 &&
                values.p99 > values.p95 && values.maximum == 5,
            "Percentile interpolation incorrect");
        FrameProfiler profiler(1000);
        for (int i = 0; i < 1000; ++i) {
            FrameSample sample;
            sample.interval_ms = 10.0 + i * 0.01;
            sample.wall_ms = 6.0;
            sample.stage_ms[static_cast<std::size_t>(FrameStage::physics)] = 2.0;
            profiler.record(sample);
        }
        require(!profiler.active() && profiler.collected() == 1000,
            "Profiler sample count mismatch");
        auto interval = profiler.statistics_interval();
        require(interval.p50 > 14.99 && interval.p50 < 15.01 &&
                interval.p95 > 19.48 && interval.p95 < 19.52 &&
                interval.p99 > 19.87 && interval.p99 < 19.92,
            "Frame interval percentile mismatch");
        require(profiler.statistics_wall().p95 == 6.0,
            "CPU wall time should be distinct from callback interval");
        require(profiler.statistics_stage(FrameStage::physics).p95 == 2.0,
            "Physics stage stats incorrect");

        FrameProfiler scoped(1000);
        {
            FrameProfileScope frame(&scoped, 0.016);
            frame.record_gpu_timing(1.5);
            frame.measure(FrameStage::scripting, [] { volatile int n = 10; (void)n; });
            frame.measure(FrameStage::animation, [] {});
            frame.measure(FrameStage::rendering, [] {
                std::vector<int> buffer;
                const int input[]{1,2,3};
                frame_memory::assign(buffer,input,input+3,
                    &NorthStarFrameMemoryCounters::geometry_copy_bytes);
                FrameProfileScope::measure_current(FrameStage::animation,[] {
                    std::vector<float> palette;
                    frame_memory::resize(palette,16);
                });
                frame_memory::assign(buffer,input,input+3,
                    &NorthStarFrameMemoryCounters::geometry_copy_bytes);
                frame_memory::device_allocated(8192);
            });
            require(frame_memory::counters==nullptr, "Frame stage leaked its thread counters");
            bool thrown = false;
            try {
                frame.measure(FrameStage::rendering, [] {
                    throw std::runtime_error("render failure");
                });
            } catch (std::runtime_error const&) { thrown = true; }
            require(thrown, "Frame profiler hid a renderer exception");
        }
        require(scoped.collected() == 1, "Frame scope did not record the frame");
        require(scoped.gpu_samples() == 1 && scoped.statistics_gpu().p50 == 1.5,
            "Optional GPU timing sample was not retained");
        require(scoped.statistics_interval().p50 == 16.0,
            "Unclamped 16ms frame interval was not recorded");
        require(scoped.statistics_stage(FrameStage::rendering).maximum >= 0,
            "Renderer stage was lost on exception");
        const auto render=scoped.memory_stage(FrameStage::rendering);
        const auto animation=scoped.memory_stage(FrameStage::animation);
        require(render.allocations==1 && render.allocation_bytes>=3*sizeof(int) &&
                render.geometry_copy_bytes==6*sizeof(int) &&
                render.device_allocations==1 && render.device_allocation_bytes==8192,
            "Rendering memory accounting mismatch");
        require(animation.allocations==1 && animation.allocation_bytes>=16*sizeof(float),
            "Nested animation memory accounting mismatch");
        std::puts("frame profiler: PASS");
        return 0;
    } catch (std::exception const& error) {
        std::fprintf(stderr, "frame profiler: FAIL: %s\n", error.what());
        return 1;
    }
}
