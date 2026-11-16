// Editor-owned UI command gates. Pure and dependency-free for smoke testing.
// No Unreal Engine source or third-party editor implementation is copied.
#pragma once

namespace northstarcpp::editor {
struct PieSessionSnapshot {
    bool process_running = false;
    bool player_attached = false;
    bool embedded = false;
    bool paused = false;
    bool ejected = false;
};
struct PieCommandAvailability {
    bool play = false;
    bool pause = false;
    bool resume = false;
    bool stop = false;
    bool eject = false;
    bool possess = false;
    bool step = false;
};
// A single executable-state query drives QAction, menus and shortcuts.
// Startup/shutdown are not "attached" and must never permit simulation IPC.
constexpr PieCommandAvailability pie_commands(PieSessionSnapshot state) noexcept {
    bool const active = state.process_running && state.embedded &&
                        state.player_attached;
    bool const possess = active && state.ejected;
    return {
        !state.process_running || possess, // Play or Possess
        active && !state.paused,            // Pause
        active && state.paused,             // Resume
        state.process_running,             // Stop
        active && !state.ejected,           // Eject
        possess,                           // Explicit session mode
        active && state.paused            // Step while paused
    };
}
// One pending step maximum. The Editor and Player exchange only a v1
// command; frame timing is selected by the Player's simulation thread.
struct PieSimulationSlice {
    float seconds=0.f;
    bool paused=true;
};
struct PieFrameStepper {
    bool pending=false;
    constexpr bool request(bool pie_paused,bool script_paused) noexcept {
        if(!pie_paused || script_paused || pending)return false;
        pending=true;
        return true;
    }
    constexpr void reset() noexcept {pending=false;}
    constexpr PieSimulationSlice advance(float raw_seconds,
                                         bool pie_paused,bool script_paused) noexcept {
        if(script_paused) {pending=false;return {0.f,true};}
        if(!pie_paused) {pending=false;return {raw_seconds,false};}
        if(!pending)return {0.f,true};
        pending=false;
        return {1.f/60.f,false}; // exactly one fixed simulation tick
    }
};
} // namespace northstarcpp::editor
