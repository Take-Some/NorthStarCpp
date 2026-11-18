// engine.vehicle scalability and deterministic iteration regression.
#include <northstarcpp/vehicle_plugin.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
static void require(bool yes,const char* why) {
    if(!yes){std::fprintf(stderr,"VEHICLE WORLD FAIL: %s\n",why);std::exit(2);}
}
int main(){
    auto const* m=northstar_module_query(NORTHSTARCPP_MODULE_ABI,"engine.vehicle",1);
    require(m && m->api_size>=sizeof(NorthStarVehicleApi),"ABI");
    auto const& api=*static_cast<NorthStarVehicleApi const*>(m->api);
    auto* w=api.create_world();
    require(w,"world");
    constexpr unsigned count=128;
    for(int id=int(count);id>=1;--id) {
        NorthStarVehicleDefinition d{};
        d.size=sizeof(d);d.entity_index=id;
        d.position[0]=float(id)*10.f;
        d.wheelbase=2.8f;d.maximum_speed=30.f;
        d.acceleration=4.f;d.braking=8.f;
        d.maximum_steering=.5f;d.rolling_drag=.01f;
        require(api.attach(w,&d),"attach");
    }
    require(api.count(w)==count,"count");
    for(unsigned i=0;i<count;++i){
        NorthStarVehicleState state{};state.size=sizeof(state);
        require(api.at(w,i,&state) && state.entity_index==i+1,
                "sorted O(log n) enumeration");
        require(api.controls(w,i+1,.6f,0.f,0.f),"controls");
    }
    auto begin=std::chrono::steady_clock::now();
    for(int step=0;step<180;++step)
        require(api.step(w,1.f/60.f),"transactional frame update");
    auto duration=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-begin).count();
    for(unsigned i=0;i<count;++i) {
        NorthStarVehicleState state{};state.size=sizeof(state);
        require(api.at(w,i,&state)&&state.entity_index==i+1 &&
            std::isfinite(state.speed) && state.speed>0.f,
            "all vehicles advanced");
    }
    require(api.detach(w,64),"detach");
    require(api.count(w)==count-1,"count after detach");
    NorthStarVehicleState after{};after.size=sizeof(after);
    require(api.at(w,63,&after) && after.entity_index==65,
            "ordinal index updates after detach");
    require(!api.detach(w,64),"reject duplicate detach");
    api.destroy_world(w);
    std::printf("vehicle world 128 entities / 180 frames: %.2f ms PASS\n",duration);
}
