#include "../engine/northstarcpp/modules/editor/window_runtime_frame_scheduler.hpp"
#include <cmath>
#include <stdexcept>
#define CHECK(condition) do { if(!(condition)) throw std::runtime_error(#condition); } while(false)
int main() {
    using northstarcpp::editor::PieFrameStepper;
    using northstarcpp::runtime_frame::advance;
    PieFrameStepper step;
    auto const regular=advance(step,0.033f,false,false,false);
    CHECK(!regular.paused && std::abs(regular.seconds-0.033f)<1e-6f);
    auto const fps=advance(step,1.0f,true,false,false);
    CHECK(!fps.paused && std::abs(fps.seconds-0.05f)<1e-6f);
    auto const freeze=advance(step,0.1f,true,true,false);
    CHECK(freeze.paused && freeze.seconds==0.f);
    CHECK(step.request(true,false));
    auto const single=advance(step,1.0f,true,true,false);
    CHECK(!single.paused && std::abs(single.seconds-1.f/60.f)<1e-6f);
    CHECK(advance(step,0.016f,true,true,false).paused);
    CHECK(step.request(true,false));
    CHECK(advance(step,0.016f,true,true,true).paused);
    CHECK(!step.pending);
}
