#include <northstarcpp/character_facing.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace northstarcpp {
namespace {
constexpr float pi=3.14159265358979323846f;
constexpr float tau=2.f*pi;
constexpr float min_movement_speed=0.15f; // m/s; ignore resting physics jitter
constexpr float turn_response_seconds=0.10f;
constexpr float max_turn_speed=10.f; // radians/sec; ~573 degrees/sec

float wrap(float value) noexcept {
    return std::remainder(value,tau);
}
} // namespace

void CharacterFacing::reset(float heading_radians)
{
    if(!std::isfinite(heading_radians))
        throw std::invalid_argument("Character heading must be finite");
    heading_=wrap(heading_radians);
}

float CharacterFacing::advance(float movement_x,float movement_z,float dt_seconds)
{
    if(!std::isfinite(movement_x) || !std::isfinite(movement_z) ||
       !std::isfinite(dt_seconds) || dt_seconds<0.f || dt_seconds>0.25f)
        throw std::invalid_argument("Invalid character facing step");
    if(dt_seconds==0.f)
        return heading_;
    auto const speed=std::hypot(movement_x,movement_z)/dt_seconds;
    // The avatar has no new travel direction when stopped or pushed by
    // tiny physics corrections, so idle does not rotate with the camera.
    if(!std::isfinite(speed) || speed<min_movement_speed)
        return heading_;
    // Teleports/scene reloads aren't a walk command; keep facing instead
    // of twisting by an arbitrary long-distance displacement.
    if(speed>35.f)
        return heading_;
    auto const target=std::atan2(movement_x,movement_z);
    auto const delta=wrap(target-heading_);
    auto const response=1.f-std::exp(-dt_seconds/turn_response_seconds);
    auto const max_step=max_turn_speed*dt_seconds;
    auto const change=std::clamp(delta*response,-max_step,max_step);
    heading_=wrap(heading_+change);
    return heading_;
}
} // namespace northstarcpp
