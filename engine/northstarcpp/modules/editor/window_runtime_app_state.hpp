#pragma once
// Player-owned PIE session state, isolated from Win32 and GPU provider ABI.
#include "qt_pie_command_state.hpp"

namespace northstarcpp::runtime_services {
struct AppSimulationState final {
    bool pie_paused=false;
    bool pie_ejected=false;
    editor::PieFrameStepper pie_stepper{};

    void reset() noexcept {
        pie_paused=false;
        pie_ejected=false;
        pie_stepper.reset();
    }
    void pause() noexcept {
        pie_paused=true;
        pie_stepper.reset();
    }
    void resume() noexcept {
        pie_paused=false;
        pie_stepper.reset();
    }
    bool request_step(bool script_paused) noexcept {
        return pie_stepper.request(pie_paused,script_paused);
    }
};
} // namespace northstarcpp::runtime_services
