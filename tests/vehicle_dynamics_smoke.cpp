// Native engine.vehicle Dynamics ABI v2: multi-wheel, tyre/suspension, seating,
// ground friction and impact resolution. Runs without GPU or V8.
#include <northstarcpp/vehicle_dynamics_plugin.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
namespace {
void check(bool ok,char const* what) {
    if(!ok){std::fprintf(stderr,"FAIL vehicle dynamics: %s\n",what);std::exit(2);}
}
NorthStarVehicleWheelConfig wheel(float x,float z,bool steer,bool drive) {
    NorthStarVehicleWheelConfig w{};
    w.size=sizeof(w);w.mount[0]=x;w.mount[1]=-.65f;w.mount[2]=z;
    w.radius=.54f;w.rest_length=.32f;w.travel=.30f;
    w.spring_rate=90000.f;w.damper_rate=9800.f;
    w.grip_long=1.05f;w.grip_lat=1.f;w.steer_factor=steer?1.f:0.f;
    w.drive_factor=drive?1.f:0.f;w.brake_factor=1.f;w.rotational_inertia=38.f;
    return w;
}
NorthStarVehicleDefinition definition() {
    NorthStarVehicleDefinition d{};
    d.size=sizeof(d);d.entity_index=7;
    d.position[0]=150.f;d.position[1]=2.f;d.position[2]=100.f;
    d.heading=0.f;d.wheelbase=6.8f;d.maximum_speed=27.f;
    d.acceleration=3.f;d.braking=8.f;d.maximum_steering=.45f;
    d.rolling_drag=.01f;return d;
}
}
int main() {
    auto* legacy=northstar_module_query(NORTHSTARCPP_MODULE_ABI,
        NORTHSTARCPP_CAPABILITY_VEHICLE,NORTHSTARCPP_VEHICLE_ABI);
    auto* ext=northstar_module_query(NORTHSTARCPP_MODULE_ABI,
        NORTHSTARCPP_CAPABILITY_VEHICLE,NORTHSTARCPP_VEHICLE_DYNAMICS_ABI);
    check(legacy&&ext,"module query versions");
    check(legacy->api_size>=sizeof(NorthStarVehicleApi)&&
          ext->api_size>=sizeof(NorthStarVehicleDynamicsApi),"ABI sizes");
    auto const& a=*static_cast<NorthStarVehicleApi const*>(legacy->api);
    auto const& d=*static_cast<NorthStarVehicleDynamicsApi const*>(ext->api);
    check(d.abi_version==2&&d.struct_size>=sizeof(d),"ABI v2 descriptor");
    std::array<NorthStarVehicleWheelConfig,8> wheels{{
        wheel(-1.f,3.45f,true,false),wheel(1.f,3.45f,true,false),
        wheel(-1.f,2.40f,true,false),wheel(1.f,2.40f,true,false),
        wheel(-1.f,-2.47f,false,true),wheel(1.f,-2.47f,false,true),
        wheel(-1.f,-3.45f,false,true),wheel(1.f,-3.45f,false,true)}};
    std::array<NorthStarVehicleSeatConfig,21> seats{};
    for(auto& s:seats){s.size=sizeof(s);s.position[1]=1.4f;}
    seats[0].role=1;seats[0].position[0]=-.78f;seats[0].position[2]=3.1f;
    for(std::size_t i=1;i<seats.size();++i) {
        seats[i].position[0]=(i%2)?-.64f:.64f;
        seats[i].position[2]=2.35f-float((i-1)/2)*.61f;
    }
    NorthStarVehicleRig rig{};
    rig.size=sizeof(rig);rig.mass=14200;rig.yaw_inertia=76000;
    rig.center_of_mass_y=1;rig.engine_force=39000;rig.brake_force=105000;
    rig.aerodynamic_drag=7.8f;rig.rolling_resistance=.012f;
    rig.abs_enabled=1;rig.traction_control_enabled=1;
    rig.wheel_count=8;rig.wheels=wheels.data();rig.seat_count=21;rig.seats=seats.data();
    void* world=a.create_world();
    void* empty=a.create_world();
    check(world&&empty,"independent worlds");
    auto spec=definition();
    check(a.attach(world,&spec)!=0,"attach");
    check(d.configure(world,7,&rig)!=0,"configure 8 wheels + seats");
    check(d.wheel_count(world,7)==8&&d.seat_count(world,7)==21,"rig dimensions");
    rig.wheel_count=17;
    check(!d.configure(world,7,&rig),"invalid excessive wheel count rejected");
    rig.wheel_count=8;
    check(d.wheel_count(world,7)==8,"invalid configuration leaves rig intact");
    check(d.occupy_seat(world,7,0,42),"occupy driver");
    check(!d.occupy_seat(world,7,1,42),"duplicate occupant rejected");
    check(!d.release_seat(world,7,0,40),"wrong occupant cannot release");
    NorthStarVehicleSeatState seat{};seat.size=sizeof(seat);
    check(d.seat_state(world,7,0,&seat)&&seat.role==1&&seat.occupant==42,
        "seat role and occupant");
    check(std::abs(seat.world_position[0]-(spec.position[0]-.78f))<.02f,
        "seat world-space transform");
    check(d.release_seat(world,7,0,42),"release driver");
    seat.size=sizeof(seat);
    check(d.seat_state(world,7,0,&seat)&&seat.occupant==0,"seat empty");
    // Host-supplied contact plane: calibrated for 0.2m compression at standstill.
    for(std::uint32_t i=0;i<8;++i) {
        NorthStarVehicleGroundContact ground{
            sizeof(NorthStarVehicleGroundContact),i,1,2.f-.65f-.54f-.32f+.20f,1.f};
        check(d.ground_contact(world,7,&ground),"ground contact applied");
    }
    check(a.controls(world,7,.9f,0.f,0.f),"apply throttle");
    for(int frame=0;frame<480;++frame)
        check(a.step(world,1.f/60.f),"8 second drive");
    NorthStarVehicleMotionState speed{};speed.size=sizeof(speed);
    check(d.motion_state(world,7,&speed),"motion telemetry");
    check(std::isfinite(speed.forward_speed)&&speed.forward_speed>2.f&&
          speed.forward_speed<27.f,"physical acceleration limit");
    check(speed.grounded_wheels==8,"eight tyres contact road");
    NorthStarVehicleWheelState tyre{};tyre.size=sizeof(tyre);
    check(d.wheel_state(world,7,6,&tyre)&&tyre.in_contact&&
          std::isfinite(tyre.angular_speed)&&tyre.normal_force>0.f,
          "wheel rpm, spring load and contact");
    check(a.controls(world,7,0.f,.35f,0.f),"steering");
    for(int frame=0;frame<120;++frame)
        check(a.step(world,1.f/60.f),"two second turn");
    speed.size=sizeof(speed);
    check(d.motion_state(world,7,&speed)&&std::isfinite(speed.yaw_rate) &&
        std::isfinite(speed.lateral_speed),"stable yaw and lateral slip");
    check(a.controls(world,7,0.f,0.f,1.f),"emergency brake");
    for(int frame=0;frame<420;++frame)
        check(a.step(world,1.f/60.f),"seven second braking");
    speed.size=sizeof(speed);
    check(d.motion_state(world,7,&speed)&&std::abs(speed.forward_speed)<.7f,
        "ABS-assisted braking stops bus");
    check(!a.controls(world,7,1.5f,0.f,0.f),"reject over-throttle");
    auto s=definition();s.entity_index=17;
    check(a.attach(empty,&s),"second world independent");
    check(a.count(world)==1&&a.count(empty)==1,"world isolation");
    // Collision is an externally resolved sweep hit from Bullet/Jolt.
    NorthStarVehicleCollision collision{};
    collision.size=sizeof(collision);collision.position[0]=151.f;
    collision.position[1]=2.f;collision.position[2]=103.f;
    collision.normal_x=0.f;collision.normal_z=-1.f;collision.restitution=.05f;
    check(d.resolve_collision(world,7,&collision),"collision correction");
    NorthStarVehicleState pose{};pose.size=sizeof(pose);
    check(a.read(world,7,&pose)&&pose.position[2]==103.f &&
        !pose.waypoint_active,"collision corrected world position");
    // All 8 tyres airborne => no road traction or load.
    for(std::uint32_t i=0;i<8;++i) {
        NorthStarVehicleGroundContact airborne{
            sizeof(NorthStarVehicleGroundContact),i,0,0.f,1.f};
        check(d.ground_contact(world,7,&airborne),"airborne contact");
    }
    check(a.controls(world,7,1.f,0.f,0.f),"airborne throttle");
    for(int i=0;i<20;++i)check(a.step(world,1.f/60.f),"airborne step");
    speed.size=sizeof(speed);
    check(d.motion_state(world,7,&speed)&&speed.grounded_wheels==0,
        "airborne tyres cannot generate ground normal load");
    a.destroy_world(empty);a.destroy_world(world);
    std::puts("engine.vehicle Dynamics ABI v2: 8 wheels, 21 seats, traction, ABS, TCS, contacts PASS");
    return 0;
}
