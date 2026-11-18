#include <northstarcpp/vehicle_powertrain_plugin.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
static void require(bool yes,const char* why){
    if(!yes){std::fprintf(stderr,"POWERTRAIN FAIL: %s\n",why);std::exit(2);}
}
int main(){
    auto* m1=northstar_module_query(NORTHSTARCPP_MODULE_ABI,"engine.vehicle",1);
    auto* m2=northstar_module_query(NORTHSTARCPP_MODULE_ABI,"engine.vehicle",2);
    auto* m3=northstar_module_query(NORTHSTARCPP_MODULE_ABI,"engine.vehicle",3);
    auto* m4=northstar_module_query(NORTHSTARCPP_MODULE_ABI,"engine.vehicle",4);
    require(m1 && m2 && m3 && m4,"all four ABI versions");
    auto const& a=*static_cast<NorthStarVehicleApi const*>(m1->api);
    auto const& p=*static_cast<NorthStarVehiclePowertrainApi const*>(m4->api);
    require(p.abi_version==4 && m4->api_size>=sizeof(p),"v4 descriptor");
    auto* w=a.create_world();
    require(w,"create world");
    NorthStarVehicleDefinition d{};
    d.size=sizeof(d);d.entity_index=17;d.wheelbase=2.7f;
    d.maximum_speed=45.f;d.acceleration=4.5f;d.braking=8.f;
    d.maximum_steering=.55f;d.rolling_drag=.01f;
    require(a.attach(w,&d),"attach vehicle");
    NorthStarVehiclePowertrainState state{};state.size=sizeof(state);
    require(!p.state(w,17,&state),"unconfigured telemetry rejected");
    NorthStarVehiclePowertrainConfig cfg{};
    cfg.size=sizeof(cfg);cfg.forward_gear_count=4;cfg.automatic=1;
    cfg.ratios[0]=3.3f;cfg.ratios[1]=2.2f;
    cfg.ratios[2]=1.4f;cfg.ratios[3]=.92f;
    cfg.reverse_ratio=3.1f;cfg.final_drive=3.8f;
    cfg.efficiency=.88f;cfg.peak_torque=300.f;cfg.idle_rpm=850.f;
    cfg.redline_rpm=6000.f;cfg.shift_up_rpm=4400.f;
    cfg.shift_down_rpm=1450.f;cfg.shift_seconds=.11f;
    auto bad=cfg;bad.ratios[1]=bad.ratios[0];
    require(!p.configure(w,17,&bad),"invalid gearing rejected");
    require(p.configure(w,17,&cfg),"valid configuration");
    require(a.controls(w,17,.9f,0.f,0.f),"accelerate");
    float max_force=0;int highest=1;
    for(int i=0;i<1200;++i){
        require(a.step(w,1.f/60.f),"20 second integration");
        state.size=sizeof(state);
        require(p.state(w,17,&state),"telemetry");
        require(std::isfinite(state.engine_rpm)&&
                state.engine_rpm>=cfg.idle_rpm&&
                state.engine_rpm<=cfg.redline_rpm,"RPM bounds");
        highest=std::max(highest,int(state.gear));
        max_force=std::max(max_force,state.wheel_force_n);
    }
    NorthStarVehicleState v{};v.size=sizeof(v);
    require(a.read(w,17,&v)&&v.speed>2.f,"driving forward");
    require(highest>1&&max_force>100.f,"automatic upshift and drive force");
    require(!p.select_gear(w,17,-1),"unsafe reverse rejected");
    require(a.controls(w,17,0.f,0.f,1.f),"brake");
    for(int i=0;i<1200;++i)require(a.step(w,1.f/60.f),"braking");
    v.size=sizeof(v);
    require(a.read(w,17,&v)&&std::abs(v.speed)<.8f,"vehicle stopped");
    require(p.select_gear(w,17,0),"manual neutral");
    require(a.controls(w,17,1.f,0.f,0.f),"neutral throttle");
    for(int i=0;i<120;++i)require(a.step(w,1.f/60.f),"neutral coast");
    state.size=sizeof(state);
    require(p.state(w,17,&state)&&state.gear==0 &&
            std::abs(state.wheel_force_n)<.0001f,"no power in neutral");
    require(p.select_gear(w,17,-1),"select reverse at rest");
    require(a.controls(w,17,-.5f,0.f,0.f),"reverse throttle");
    float negative_force=0;
    for(int i=0;i<150;++i){
        require(a.step(w,1.f/60.f),"reverse step");
        state.size=sizeof(state);
        require(p.state(w,17,&state),"reverse telemetry");
        negative_force=std::min(negative_force,state.wheel_force_n);
    }
    require(negative_force< -100.f,"negative reverse torque");
    a.destroy_world(w);
    std::puts("Powertrain v4 shift, reverse, neutral, telemetry, legacy ABIs PASS");
}
