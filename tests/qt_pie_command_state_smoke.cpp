// NorthStar PIE action availability tests: intentionally no Qt dependency.
#include "qt_pie_command_state.hpp"
#include <cassert>
#include <iostream>
using northstarcpp::editor::PieSessionSnapshot;
using northstarcpp::editor::pie_commands;

int main() {
    {
        constexpr auto idle=pie_commands({});
        static_assert(idle.play && !idle.pause && !idle.resume &&
                      !idle.stop && !idle.eject && !idle.possess);
    }
    {
        constexpr auto launching=pie_commands({true,false,true,false,false});
        static_assert(!launching.play && !launching.pause && !launching.resume &&
                      launching.stop && !launching.eject && !launching.possess);
    }
    {
        constexpr auto running=pie_commands({true,true,true,false,false});
        static_assert(!running.play && running.pause && !running.resume &&
                      running.stop && running.eject && !running.possess);
    }
    {
        constexpr auto paused=pie_commands({true,true,true,true,false});
        static_assert(!paused.play && !paused.pause && paused.resume &&
                      paused.stop && paused.eject && !paused.possess);
    }
    {
        constexpr auto ejected=pie_commands({true,true,true,false,true});
        static_assert(ejected.play && ejected.pause && !ejected.resume &&
                      ejected.stop && !ejected.eject && ejected.possess);
    }
    // Check all 32 possible snapshots: invalid combinations must not leak
    // commands to a detached or stopped Player.
    for(unsigned mask=0;mask<32;++mask) {
        auto const state=PieSessionSnapshot{
            bool(mask&1),bool(mask&2),bool(mask&4),
            bool(mask&8),bool(mask&16)};
        auto const actions=pie_commands(state);
        auto const attached=state.process_running && state.embedded &&
                            state.player_attached;
        assert(actions.play==(!state.process_running || (attached && state.ejected)));
        assert(actions.stop==state.process_running);
        assert(actions.pause==(attached && !state.paused));
        assert(actions.resume==(attached && state.paused));
        assert(actions.eject==(attached && !state.ejected));
        assert(actions.possess==(attached && state.ejected));
        assert(!(actions.pause && actions.resume));
        assert(actions.step==(attached && state.paused));
    }
    {
        northstarcpp::editor::PieFrameStepper clock;
        auto running=clock.advance(.025f,false,false);
        assert(!running.paused && running.seconds==.025f);
        auto paused=clock.advance(99.f,true,false);
        assert(paused.paused && paused.seconds==0.f);
        assert(clock.request(true,false));
        assert(!clock.request(true,false)); // never queue multiple catch-up ticks
        auto step=clock.advance(10.f,true,false);
        assert(!step.paused && step.seconds==1.f/60.f);
        auto next=clock.advance(30.f,true,false);
        assert(next.paused && next.seconds==0.f);
        assert(clock.request(true,false));
        clock.reset(); // Resume must cancel a pending manual step
        auto resumed=clock.advance(.012f,false,false);
        assert(!resumed.paused && resumed.seconds==.012f);
        assert(!clock.request(false,false));
        assert(!clock.request(true,true)); // respect scripted pause
    }
    std::cout << "NorthStar PIE commands and fixed-frame Step: PASS (32 states)\n";
}
