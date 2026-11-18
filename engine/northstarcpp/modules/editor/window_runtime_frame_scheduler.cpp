#include "window_runtime_frame_scheduler.hpp"
#include <algorithm>

namespace northstarcpp::runtime_frame {
SimulationSlice advance(editor::PieFrameStepper& stepper,
                        float raw_seconds,bool first_person,
                        bool pie_paused,bool script_paused) noexcept {
    if(first_person)
        raw_seconds=std::clamp(raw_seconds,0.f,0.05f);
    auto const slice=stepper.advance(raw_seconds,pie_paused,script_paused);
    return {slice.seconds,slice.paused};
}
} // namespace northstarcpp::runtime_frame
