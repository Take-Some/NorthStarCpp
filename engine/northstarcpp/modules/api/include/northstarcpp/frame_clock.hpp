// NorthStarCpp 2026 — one monotonic clock for Player and Qt Editor.
// Uses the retained foundation QPC implementation in timing_functions_win.cpp.
#pragma once
#include <cstdint>
namespace northstarcpp::timing {
class FrameClock final {
public:
    void start();
    double restart_seconds();
    double elapsed_seconds() const;
private:
    std::uint64_t begin_ = 0;
    bool started_ = false;
};
} // namespace northstarcpp::timing
