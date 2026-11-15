// NorthStarCpp 2026 — movement-driven character orientation.
// Gameplay owns facing; camera controls and the render-provider ABI remain independent.
#pragma once
#include <northstarcpp/scene.hpp>

namespace northstarcpp {

// Converts horizontal world-space travel into a smooth +Z-forward yaw.
// Coordinate convention matches FpsArena::direction(): yaw 0 -> +Z,
// yaw +pi/2 -> +X. Stops retain the most recent heading.
class CharacterFacing {
public:
    void reset(float heading_radians = 0.f);
    float advance(float movement_x, float movement_z, float dt_seconds);
    float heading() const noexcept { return heading_; }

private:
    float heading_ = 0.f;
};

} // namespace northstarcpp
