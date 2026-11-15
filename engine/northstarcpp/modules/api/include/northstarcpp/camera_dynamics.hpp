// NorthStarCpp: independent, allocation-free camera dynamics.
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// Exact integration for a damped second-order spring with a fixed goal.
#pragma once
#include <northstarcpp/scene.hpp>
#include <algorithm>
#include <cmath>

namespace northstarcpp::camera_dynamics {

struct SpringState {
    Vector3 velocity{0.f,0.f,0.f};
    void reset() noexcept { velocity={0.f,0.f,0.f}; }
};

// The same response matrix is applied to X/Y/Z, so costly transcendentals
// are computed once per frame rather than for every coordinate.
struct SpringResponse {
    float position_from_offset=1.f;
    float position_from_velocity=0.f;
    float velocity_from_offset=0.f;
    float velocity_from_velocity=1.f;
};

inline SpringResponse response(float delta_seconds,float frequency_hz,
                               float damping_ratio) noexcept {
    if(!(delta_seconds>0.f) || !(frequency_hz>0.f))
        return {};
    constexpr float pi=3.14159265358979323846f;
    auto const omega=2.f*pi*frequency_hz;
    auto const attenuation=damping_ratio*omega;
    if(std::abs(damping_ratio-1.f)<1.e-4f) {
        auto const decay=std::exp(-omega*delta_seconds);
        return {
            decay*(1.f+omega*delta_seconds),
            decay*delta_seconds,
            -decay*omega*omega*delta_seconds,
            decay*(1.f-omega*delta_seconds)
        };
    }
    if(damping_ratio<1.f) {
        auto const oscillation=omega*std::sqrt(
            std::max(0.f,1.f-damping_ratio*damping_ratio));
        auto const decay=std::exp(-attenuation*delta_seconds);
        auto const cosine=std::cos(oscillation*delta_seconds);
        auto const sine=std::sin(oscillation*delta_seconds)/oscillation;
        return {
            decay*(cosine+attenuation*sine),
            decay*sine,
            -decay*omega*omega*sine,
            decay*(cosine-attenuation*sine)
        };
    }
    auto const spread=omega*std::sqrt(
        damping_ratio*damping_ratio-1.f);
    auto const slow=-attenuation+spread;
    auto const fast=-attenuation-spread;
    auto const slow_decay=std::exp(slow*delta_seconds);
    auto const fast_decay=std::exp(fast*delta_seconds);
    auto const difference=slow-fast;
    auto const velocity_factor=(slow_decay-fast_decay)/difference;
    return {
        (slow_decay*(-fast)+fast_decay*slow)/difference,
        velocity_factor,
        -omega*omega*velocity_factor,
        (slow*slow_decay-fast*fast_decay)/difference
    };
}

inline float integrate(float position,float goal,float& velocity,
                       SpringResponse const& m) noexcept {
    auto const offset=position-goal;
    auto const next_velocity=m.velocity_from_offset*offset+
                             m.velocity_from_velocity*velocity;
    auto const next=goal+m.position_from_offset*offset+
                    m.position_from_velocity*velocity;
    velocity=next_velocity;
    return next;
}

inline Vector3 advance(Vector3 position,Vector3 goal,SpringState& spring,
                       float delta_seconds,float frequency_hz,
                       float damping_ratio=1.f) noexcept {
    if(!(frequency_hz>0.f)) {
        spring.reset();
        return goal;
    }
    auto const m=response(delta_seconds,frequency_hz,damping_ratio);
    return {
        integrate(position.x,goal.x,spring.velocity.x,m),
        integrate(position.y,goal.y,spring.velocity.y,m),
        integrate(position.z,goal.z,spring.velocity.z,m)
    };
}

inline float exponential_alpha(float delta_seconds,float response_seconds) noexcept {
    return response_seconds>0.f ?
        -std::expm1(-delta_seconds/response_seconds) : 1.f;
}

} // namespace northstarcpp::camera_dynamics
