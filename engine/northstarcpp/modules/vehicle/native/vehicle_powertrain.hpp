#pragma once
// Original, deterministic longitudinal powertrain model. No RAGE code.
#include <northstarcpp/vehicle_powertrain_schema.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace northstarcpp::vehicle_physics {
struct Powertrain {
    NorthStarVehiclePowertrainConfig config{};
    NorthStarVehiclePowertrainState state{};
    bool enabled=false;
    bool has_manual_override=false;
};
inline void configure_powertrain(Powertrain& p,
    NorthStarVehiclePowertrainConfig const& config) {
    if(!vehicle_schema::valid_powertrain(config))
        throw std::invalid_argument("Invalid powertrain ratios, shift thresholds or torque");
    p.config=config;
    p.enabled=true;
    p.has_manual_override=false;
    p.state={sizeof(p.state),1,config.idle_rpm,0,0,0};
}
inline void select_gear(Powertrain& p,int gear,float forward_speed) {
    if(!p.enabled || gear< -1 || gear>int(p.config.forward_gear_count))
        throw std::invalid_argument("Powertrain gear outside configured range");
    // Disallow switching from forward to reverse at a dangerous speed.
    if((gear==-1 && forward_speed>0.8f) ||
       (gear>0 && forward_speed<-.8f))
        throw std::invalid_argument("Powertrain reverse direction while moving");
    if(gear!=p.state.gear) {
        p.state.gear=gear;
        p.state.shift_remaining_s=p.config.shift_seconds;
    }
    p.has_manual_override=true;
}
inline float update_powertrain(Powertrain& p,float dt,float speed,
                               float throttle,float driven_radius) {
    if(!p.enabled)return 0.f;
    auto const& c=p.config;
    auto& s=p.state;
    float const radius=std::max(.12f,driven_radius);
    // Smoothly track wheel-coupled RPM; keep the engine above idle.
    // Gear 0 is free-revving and transmits no drive torque.
    float const ratio=s.gear>0?c.ratios[s.gear-1]:
                      s.gear==-1?c.reverse_ratio:0.f;
    float const wheel_rpm=std::abs(speed)*60.f/(6.28318530718f*radius);
    float const requested=std::clamp(
        std::max(c.idle_rpm,wheel_rpm*ratio*c.final_drive+
                 (std::abs(speed)<1.f?std::abs(throttle)*350.f:0.f)),
        c.idle_rpm,c.redline_rpm);
    s.engine_rpm=std::clamp(
        s.engine_rpm+(requested-s.engine_rpm)*std::min(1.f,dt*12.f),
        c.idle_rpm,c.redline_rpm);
    if(c.automatic && !p.has_manual_override && s.shift_remaining_s<=0.f) {
        int desired=s.gear;
        // At a near-stop, negative gas selects the reverse train.
        if(speed<.45f && speed>-.45f) {
            if(throttle<-.05f)desired=-1;
            else if(throttle>.05f && desired<=0)desired=1;
        } else if(s.gear>0) {
            if(s.engine_rpm>=c.shift_up_rpm &&
               s.gear<int(c.forward_gear_count))++desired;
            else if(s.engine_rpm<=c.shift_down_rpm && s.gear>1)--desired;
        }
        if(desired!=s.gear) {
            s.gear=desired;
            s.shift_remaining_s=c.shift_seconds;
        }
    }
    if(s.shift_remaining_s>0.f) {
        s.shift_remaining_s=std::max(0.f,s.shift_remaining_s-dt);
        s.delivered_torque_nm=0.f;
        s.wheel_force_n=0.f;
        return 0.f;
    }
    float const effective_ratio=s.gear>0?c.ratios[s.gear-1]:
                                s.gear==-1?c.reverse_ratio:0.f;
    float const command= s.gear==-1?std::max(0.f,-throttle):
                         s.gear>0?std::max(0.f,throttle):0.f;
    // Soft high-RPM torque reduction; no negative torque produced at idle.
    float const rpm_frac=s.engine_rpm/c.redline_rpm;
    float const output=command*c.peak_torque*
        std::max(.4f,1.f-.4f*rpm_frac*rpm_frac);
    s.delivered_torque_nm=output;
    float const force=output*effective_ratio*c.final_drive*c.efficiency/radius;
    s.wheel_force_n=s.gear==-1?-force:force;
    return std::clamp(s.wheel_force_n,-1.e6f,1.e6f);
}
} // namespace northstarcpp::vehicle_physics
