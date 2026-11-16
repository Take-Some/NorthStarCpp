// NorthStar C++ native vehicle dynamics (planar multi-wheel tyre model).
// No graphics, ECS, V8 or plugin-loader dependency. Units are SI.
// This is NOT Jolt VehicleConstraint: ground/impact contacts are supplied by host.
#pragma once
#include <northstarcpp/vehicle_cabin_plugin.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace northstarcpp::vehicle_physics {
inline constexpr float pi=3.14159265358979323846f;
inline constexpr float gravity=9.80665f;
inline float clamp(float v,float lo,float hi) {return std::clamp(v,lo,hi);}
inline float angle(float x) {return std::remainder(x,2.f*pi);}
inline float sign(float x) {return (x>0.f)-(x<0.f);}
inline float rotate_x(float x,float z,float heading) {
    return x*std::cos(heading)+z*std::sin(heading);
}
inline float rotate_z(float x,float z,float heading) {
    return z*std::cos(heading)-x*std::sin(heading);
}
struct Wheel {
    NorthStarVehicleWheelConfig setup{};
    NorthStarVehicleWheelState telemetry{};
    bool explicit_contact=false;
    float ground_y=0.f;
    float surface_mu=1.f;
};
struct Seat {
    NorthStarVehicleSeatConfig setup{};
    std::uint64_t occupant=0;
};
struct Door {
    NorthStarVehicleDoorConfig setup{};
    float fraction=0.f;
    float target=0.f;
};
struct Vehicle {
    NorthStarVehicleDefinition definition{};
    NorthStarVehicleState state{};
    std::vector<Wheel> wheels;
    std::vector<Seat> seats;
    std::vector<Door> doors;
    float mass=1500.f;
    float yaw_inertia=3200.f;
    float center_of_mass_y=0.65f;
    float engine_force=3600.f;
    float brake_force=8250.f;
    float aerodynamic_drag=0.55f;
    float rolling_resistance=0.015f;
    bool abs_enabled=true;
    bool traction_enabled=true;
    float forward_speed=0.f;
    float lateral_speed=0.f;
    float yaw_rate=0.f;
    float distance_travelled=0.f;
    float last_long_slip=0.f;
    float last_lat_slip=0.f;
    float last_forward_accel=0.f;
    float last_lateral_accel=0.f;
    std::uint32_t grounded_wheels=0;
    float target_x=0, target_z=0, target_speed=0, arrival_radius=2.f;
};
inline Wheel default_wheel(float x,float z,bool steer,bool driven) {
    Wheel w;
    auto& c=w.setup;
    c.size=sizeof(c);
    c.mount[0]=x;c.mount[1]=-0.5f;c.mount[2]=z;
    c.radius=0.34f;c.rest_length=0.25f;c.travel=0.28f;
    c.spring_rate=24000.f;c.damper_rate=3000.f;
    c.grip_long=1.0f;c.grip_lat=1.0f;
    c.steer_factor=steer?1.f:0.f;
    c.drive_factor=driven?1.f:0.f;
    c.brake_factor=1.f;
    c.rotational_inertia=2.f;
    w.telemetry.size=sizeof(w.telemetry);
    w.telemetry.in_contact=1u; // fallback flat surface for ABI v1
    return w;
}
inline void default_rig(Vehicle& v) {
    float const z=v.definition.wheelbase*0.5f;
    v.wheels={default_wheel(-0.8f,z,true,false),
              default_wheel(0.8f,z,true,false),
              default_wheel(-0.8f,-z,false,true),
              default_wheel(0.8f,-z,false,true)};
    v.mass=1500.f;v.yaw_inertia=3200.f;
    v.engine_force=v.mass*v.definition.acceleration;
    v.brake_force=v.mass*v.definition.braking;
    v.rolling_resistance=0.015f;
    v.aerodynamic_drag=0.55f;
}
inline void guide(Vehicle& v) {
    auto& s=v.state;
    if(!s.waypoint_active)return;
    float const dx=v.target_x-s.position[0],dz=v.target_z-s.position[2];
    float const distance=std::hypot(dx,dz);
    if(distance<=v.arrival_radius && std::abs(v.forward_speed)<0.35f) {
        s.waypoint_active=0;
        s.throttle=0;s.brake=1;s.steering=0;
        return;
    }
    float const target_angle=std::atan2(dx,dz);
    float const difference=angle(target_angle-s.heading);
    s.steering=clamp(difference/std::max(0.1f,v.definition.maximum_steering*1.5f),-1.f,1.f);
    float const stopping=std::sqrt(2.f*std::max(.2f,v.definition.braking)*
        std::max(0.f,distance-v.arrival_radius));
    float desired=std::min(v.target_speed,stopping);
    // Reduce target speed on tight corners; no reverse driving into targets.
    desired=std::min(desired, std::max(1.5f,v.target_speed/(1.f+2.f*std::abs(difference))));
    float const difference_speed=desired-v.forward_speed;
    s.throttle=clamp(difference_speed*0.6f,0.f,1.f);
    s.brake=clamp(-difference_speed*0.35f,0.f,1.f);
    if(distance<=v.arrival_radius) {s.throttle=0;s.brake=1;}
}
inline bool step_substep(Vehicle& v,float dt) {
    if(dt==0.f)return true;
    guide(v);
    for(auto& door:v.doors) {
        float const rate=door.setup.angular_speed/
            std::max(.01f,std::abs(door.setup.open_angle));
        float const travel=rate*dt;
        if(door.fraction<door.target)
            door.fraction=std::min(door.target,door.fraction+travel);
        else
            door.fraction=std::max(door.target,door.fraction-travel);
    }
    float const heading=v.state.heading;
    float const speed=std::hypot(v.forward_speed,v.lateral_speed);
    float const steer_scale=1.f/(1.f+speed*speed/(25.f*25.f));
    float force_forward=0.f,force_lateral=0.f,yaw_torque=0.f;
    float driven_weight=0.f,brake_weight=0.f;
    for(auto const& w:v.wheels) {
        driven_weight+=w.setup.drive_factor;
        brake_weight+=w.setup.brake_factor;
    }
    float largest_long_slip=0.f,largest_lat_slip=0.f;
    std::uint32_t contacted=0u;
    float const nominal_load=v.mass*gravity/static_cast<float>(v.wheels.size());
    float z2=0.f,x2=0.f;
    for(auto const& w:v.wheels) {
        z2+=w.setup.mount[2]*w.setup.mount[2];
        x2+=w.setup.mount[0]*w.setup.mount[0];
    }
    z2=std::max(z2,0.1f);x2=std::max(x2,0.1f);
    for(auto& w:v.wheels) {
        auto const& c=w.setup;
        auto& t=w.telemetry;
        float const longitudinal_load=-v.mass*v.center_of_mass_y*
            v.last_forward_accel*c.mount[2]/z2;
        float const lateral_load=-v.mass*v.center_of_mass_y*
            (v.last_lateral_accel+v.yaw_rate*v.forward_speed)*c.mount[0]/x2;
        float const load_transfer=clamp(longitudinal_load+lateral_load,
            -nominal_load,3.f*nominal_load);
        t.steer_angle=v.state.steering*v.definition.maximum_steering*
            steer_scale*c.steer_factor;
        float const previous_compression=t.compression;
        if(w.explicit_contact) {
            if(t.in_contact) {
                float const wheel_height=v.state.position[1]+c.mount[1];
                float const available=c.rest_length+c.radius+w.ground_y-wheel_height;
                t.compression=clamp(available,0.f,c.travel);
                float const compression_rate=clamp(
                    (t.compression-previous_compression)/dt,-20.f,20.f);
                t.normal_force=clamp(c.spring_rate*t.compression+
                    c.damper_rate*compression_rate+load_transfer,0.f,4.f*nominal_load);
                if(available<0.f)t.in_contact=0;
            } else {t.compression=0.f;t.normal_force=0.f;}
        } else {
            t.in_contact=1;
            t.normal_force=clamp(nominal_load+load_transfer,0.f,4.f*nominal_load);
            t.compression=clamp(nominal_load/c.spring_rate,0.f,c.travel);
        }
        if(!t.in_contact || t.normal_force<=0.f) {
            t.normal_force=0.f;
        } else ++contacted;

        float const wx=v.lateral_speed+v.yaw_rate*c.mount[2];
        float const wz=v.forward_speed-v.yaw_rate*c.mount[0];
        float const cosine=std::cos(t.steer_angle),sine=std::sin(t.steer_angle);
        float const along=wz*cosine+wx*sine;
        float const across=wx*cosine-wz*sine;
        float const mu=w.surface_mu;
        float const cap_long=t.normal_force*mu*c.grip_long;
        float const cap_lat=t.normal_force*mu*c.grip_lat;
        float const denominator=std::max(2.f,std::abs(along));
        t.slip_ratio=(t.angular_speed*c.radius-along)/denominator;
        t.slip_angle=std::atan2(across,std::max(1.f,std::abs(along)));
        float const slip_for_force=clamp(t.slip_ratio,-5.f,5.f);
        float const forward_tyre=cap_long*std::tanh(4.f*slip_for_force);
        float const sideways_tyre=-cap_lat*std::tanh(3.f*t.slip_angle);
        // Friction circle: full longitudinal and lateral force cannot coexist.
        float long_force=forward_tyre,side_force=sideways_tyre;
        if(cap_long>0.f && cap_lat>0.f) {
            float const usage=std::hypot(long_force/cap_long,side_force/cap_lat);
            if(usage>1.f) {long_force/=usage;side_force/=usage;}
        }
        float const force_x=long_force*sine+side_force*cosine;
        float const force_z=long_force*cosine-side_force*sine;
        force_forward+=force_z;
        force_lateral+=force_x;
        yaw_torque+=c.mount[2]*force_x-c.mount[0]*force_z;
        float drive=driven_weight>0.f?
            v.state.throttle*v.engine_force*c.radius*
            (c.drive_factor/driven_weight):0.f;
        if(v.traction_enabled && std::abs(t.slip_ratio)>0.25f &&
           drive*t.slip_ratio>0.f)
            drive*=0.3f;
        float brake=brake_weight>0.f?
            v.state.brake*v.brake_force*c.radius*
            (c.brake_factor/brake_weight):0.f;
        if(v.abs_enabled && t.slip_ratio<-.2f && along>1.f)brake*=.65f;
        float const rotation_direction=std::abs(t.angular_speed)>0.02f?
            sign(t.angular_speed):sign(along);
        float const opposing=brake*rotation_direction;
        float omega=t.angular_speed+
            (drive-opposing-long_force*c.radius)/c.rotational_inertia*dt;
        if(brake>0.f && drive==0.f && t.angular_speed*omega<0.f)
            omega=0.f;
        t.angular_speed=clamp(omega,-1000.f,1000.f);
        t.spin_angle=angle(t.spin_angle+t.angular_speed*dt);
        largest_long_slip=std::max(largest_long_slip,std::abs(t.slip_ratio));
        largest_lat_slip=std::max(largest_lat_slip,std::abs(t.slip_angle));
    }
    // Mechanical/air resistance: never create motion from drag at standstill.
    if(std::abs(v.forward_speed)>0.002f) {
        force_forward-=sign(v.forward_speed)*
            (v.mass*gravity*v.rolling_resistance+
             v.aerodynamic_drag*v.forward_speed*v.forward_speed);
    }
    if(std::abs(v.lateral_speed)>0.002f)
        force_lateral-=v.aerodynamic_drag*v.lateral_speed*
            std::abs(v.lateral_speed);
    // Body-frame translational dynamics including yaw cross-coupling.
    float new_forward=v.forward_speed+
        (force_forward/v.mass+v.yaw_rate*v.lateral_speed)*dt;
    float new_side=v.lateral_speed+
        (force_lateral/v.mass-v.yaw_rate*v.forward_speed)*dt;
    // Avoid drag/brake oscillations through zero, and enforce design speed.
    if(std::abs(new_forward)<.001f)new_forward=0.f;
    new_forward=clamp(new_forward,-0.35f*v.definition.maximum_speed,
                     v.definition.maximum_speed);
    float new_yaw=v.yaw_rate+yaw_torque/v.yaw_inertia*dt;
    new_yaw=clamp(new_yaw,-3.f,3.f);
    if(std::abs(new_yaw)<.00001f)new_yaw=0.f;
    new_side=clamp(new_side,-v.definition.maximum_speed,
                   v.definition.maximum_speed);
    float const new_heading=angle(heading+new_yaw*dt);
    float const dx=rotate_x(new_side,new_forward,new_heading)*dt;
    float const dz=rotate_z(new_side,new_forward,new_heading)*dt;
    if(!std::isfinite(dx)||!std::isfinite(dz)||
       std::abs(v.state.position[0]+dx)>=1000000.f ||
       std::abs(v.state.position[2]+dz)>=1000000.f)
        return false;
    v.last_forward_accel=clamp((new_forward-v.forward_speed)/dt,-30.f,30.f);
    v.last_lateral_accel=clamp((new_side-v.lateral_speed)/dt,-30.f,30.f);
    v.forward_speed=new_forward;
    v.lateral_speed=new_side;
    v.yaw_rate=new_yaw;
    v.state.heading=new_heading;
    v.state.speed=new_forward;
    v.state.position[0]+=dx;
    v.state.position[2]+=dz;
    v.distance_travelled+=std::hypot(dx,dz);
    v.last_long_slip=largest_long_slip;
    v.last_lat_slip=largest_lat_slip;
    v.grounded_wheels=contacted;
    return true;
}
// Fixed internal wheel substeps prevent stiff tyre/rotational inertia instability.
inline bool step(Vehicle& v,float dt) {
    if(dt<=0.f)return true;
    auto const segments=std::max(1,static_cast<int>(std::ceil(dt*240.f)));
    float const h=dt/static_cast<float>(segments);
    for(int i=0;i<segments;++i)if(!step_substep(v,h))return false;
    return true;
}
} // namespace northstarcpp::vehicle_physics
