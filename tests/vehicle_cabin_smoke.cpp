// Native engine.vehicle Cabin ABI v3: timed, validated doors and actuator safety.
#include <northstarcpp/vehicle_cabin_plugin.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
int main() {
    auto* core=northstar_module_query(NORTHSTARCPP_MODULE_ABI,
        NORTHSTARCPP_CAPABILITY_VEHICLE,NORTHSTARCPP_VEHICLE_ABI);
    auto* cabin=northstar_module_query(NORTHSTARCPP_MODULE_ABI,
        NORTHSTARCPP_CAPABILITY_VEHICLE,NORTHSTARCPP_VEHICLE_CABIN_ABI);
    if(!core||!cabin || cabin->api_size<sizeof(NorthStarVehicleCabinApi))return 1;
    auto const& base=*static_cast<NorthStarVehicleApi const*>(core->api);
    auto const& doors=*static_cast<NorthStarVehicleCabinApi const*>(cabin->api);
    auto* world=base.create_world();
    if(!world)return 2;
    NorthStarVehicleDefinition config{};
    config.size=sizeof(config);config.entity_index=1;
    config.wheelbase=6.2f;config.maximum_speed=18.f;config.acceleration=3.f;
    config.braking=6.f;config.maximum_steering=.5f;config.rolling_drag=.07f;
    if(!base.attach(world,&config))return 3;
    NorthStarVehicleDoorConfig d[2]{};
    for(auto& v:d) {
        v.size=sizeof(v);v.angular_speed=1.0f;v.open_angle=1.2f;
    }
    d[0].hinge[0]=-1.f;d[1].hinge[0]=1.f;d[1].open_angle=-1.2f;
    if(!doors.configure_doors(world,1,d,2)||doors.door_count(world,1)!=2)
        return 4;
    if(!doors.set_door(world,1,0,1)||doors.set_door(world,1,8,1))
        return 5;
    for(int n=0;n<110;++n)if(!base.step(world,1.f/60.f))return 6;
    NorthStarVehicleDoorState state{};state.size=sizeof(state);
    if(!doors.door_state(world,1,0,&state) || state.fraction<.98f ||
       state.target_fraction!=1.f || state.angle<1.17f)return 7;
    if(!doors.set_door(world,1,0,0))return 8;
    for(int n=0;n<110;++n)if(!base.step(world,1.f/60.f))return 9;
    state.size=sizeof(state);
    if(!doors.door_state(world,1,0,&state) || state.fraction>.01f)return 10;
    if(!base.controls(world,1,1.f,0.f,0.f))return 11;
    for(int n=0;n<180;++n)if(!base.step(world,1.f/60.f))return 12;
    if(doors.set_door(world,1,0,1))return 13;
    if(!doors.set_door(world,1,0,0))return 14;
    base.destroy_world(world);
    std::puts("engine.vehicle Cabin v3: 2 doors, time animation, driving lockout PASS");
    return 0;
}
