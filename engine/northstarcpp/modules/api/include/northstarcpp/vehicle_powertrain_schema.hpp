#pragma once
#include <northstarcpp/vehicle_powertrain_plugin.h>
#include <cmath>
namespace northstarcpp::vehicle_schema {
inline bool valid_powertrain(NorthStarVehiclePowertrainConfig const& c) {
    if(c.size<sizeof(c) || c.forward_gear_count<1 ||
       c.forward_gear_count>NORTHSTARCPP_VEHICLE_MAX_FORWARD_GEARS ||
       c.automatic>1)return false;
    auto const valid=[](float x,float lo,float hi) {
        return std::isfinite(x)&&x>=lo&&x<=hi;
    };
    if(!valid(c.reverse_ratio,.1f,15.f) ||
       !valid(c.final_drive,.1f,25.f) ||
       !valid(c.efficiency,.1f,1.f) ||
       !valid(c.peak_torque,10.f,10000.f) ||
       !valid(c.idle_rpm,300.f,3000.f) ||
       !valid(c.redline_rpm,1500.f,15000.f) ||
       !valid(c.shift_seconds,0.f,2.f) ||
       !(c.redline_rpm>c.idle_rpm+500.f) ||
       !(c.shift_down_rpm>c.idle_rpm) ||
       !(c.shift_up_rpm>c.shift_down_rpm+200.f) ||
       !(c.shift_up_rpm<=c.redline_rpm))return false;
    for(unsigned i=0;i<c.forward_gear_count;++i)
        if(!valid(c.ratios[i],.1f,20.f) ||
           (i && c.ratios[i]>=c.ratios[i-1]))return false;
    return true;
}
}
