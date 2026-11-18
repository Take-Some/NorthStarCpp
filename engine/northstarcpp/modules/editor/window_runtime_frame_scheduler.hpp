#pragma once
// Fixed single-step timing policy shared by Player's runtime frame loop.
// No heap allocation or renderer/plugin ABI dependency.
#include "qt_pie_command_state.hpp"

namespace northstarcpp::runtime_frame {
struct SimulationSlice {
    float seconds=0.f;
    bool paused=true;
};
// Keep the raw interval in the caller for profiling. Only simulation delta
// is clamped; a PIE single-step still advances by one exact fixed tick.
SimulationSlice advance(editor::PieFrameStepper& stepper,
                        float raw_seconds,bool first_person,
                        bool pie_paused,bool script_paused) noexcept;
} // namespace northstarcpp::runtime_frame
