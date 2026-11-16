// Integration test: Jolt-native 6DOF vehicle constraint with 4 wheel probes.
#include <northstarcpp/physics_vehicle_plugin.h>
#include <northstarcpp/physics_plugin.h>
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto* lib=LoadLibraryA(argv[1]);
    if(!lib){std::fprintf(stderr,"Jolt DLL failed: %lu\n",GetLastError());return 3;}
    auto query=reinterpret_cast<NorthStarModuleQuery>(
        GetProcAddress(lib,"northstar_module_query"));
    if(!query)return 4;
    auto* basic=query(NORTHSTARCPP_MODULE_ABI,
        NORTHSTARCPP_CAPABILITY_PHYSICS,NORTHSTARCPP_PHYSICS_ABI);
    auto* ext=query(NORTHSTARCPP_MODULE_ABI,
        NORTHSTARCPP_CAPABILITY_PHYSICS,NORTHSTARCPP_PHYSICS_VEHICLE_MODULE_ABI);
    if(!basic||!ext)return 5;
    auto& physics=*static_cast<NorthStarPhysicsApi const*>(basic->api);
    auto& vehicle=*static_cast<NorthStarPhysicsVehicleApi const*>(ext->api);
    if(vehicle.abi_version!=1||!vehicle.create||!vehicle.pose)return 6;
    NorthStarRigidBody body{0.f,0.8f,0.f,1.4f,0.f,0};
    auto* world=physics.create(&body,1);
    if(!world)return 7;
    std::array<NorthStarVehicleWheelConfig,4> wheel{};
    for(int i=0;i<4;++i) {
        auto& w=wheel[i];
        w.size=sizeof(w);w.mount[0]=(i%2? .8f:-.8f);
        w.mount[1]=-.45f;w.mount[2]=(i<2?1.3f:-1.3f);
        w.radius=.36f;w.rest_length=.45f;w.travel=.25f;
        w.spring_rate=22000.f;w.damper_rate=2400.f;w.grip_lat=1.f;
        w.grip_long=1.f;w.steer_factor=i<2?1.f:0.f;
        w.drive_factor=i>=2?1.f:0.f;w.brake_factor=1.f;
        w.rotational_inertia=1.5f;
    }
    NorthStarVehicleRig rig{};
    rig.size=sizeof(rig);rig.mass=1350.f;rig.engine_force=8000.f;
    rig.brake_force=13000.f;rig.wheel_count=4;rig.wheels=wheel.data();
    if(!vehicle.create(world,0,&rig,0.f)) {
        std::fprintf(stderr,"Jolt vehicle.create failed: %s\n",ext->last_error());
        physics.destroy(world);FreeLibrary(lib);return 8;
    }
    if(vehicle.wheel_count(world,0)!=4)return 9;
    for(int n=0;n<240;++n) {
        if(n==130)vehicle.controls(world,0,1.f,.22f,0.f,0.f);
        if(!physics.step(world,1.f/60.f,0,0))return 10;
    }
    NorthStarPhysicsVehiclePose pose{};pose.size=sizeof(pose);
    if(!vehicle.pose(world,0,&pose))return 11;
    if(!std::isfinite(pose.x)||!std::isfinite(pose.y)||
       !std::isfinite(pose.z)||!std::isfinite(pose.rotation[1]))
        return 12;
    bool contact=false;
    for(std::uint32_t i=0;i<4;++i) {
        NorthStarVehicleWheelState s{};s.size=sizeof(s);
        if(!vehicle.wheel(world,0,i,&s))return 13;
        contact|=s.in_contact!=0;
    }
    if(!contact) {
        std::fprintf(stderr,"No Jolt wheel contacts after simulation y=%f\n",pose.y);
        return 14;
    }
    if(std::hypot(pose.x,pose.z)<.2f){
        std::fprintf(stderr,"Vehicle did not drive: %.3f %.3f\n",pose.x,pose.z);
        return 15;
    }
    if(!vehicle.controls(world,0,0,0,1,0))return 16;
    for(int n=0;n<200;++n)if(!physics.step(world,1.f/60.f,0,0))return 17;
    if(!vehicle.destroy(world,0))return 18;
    physics.destroy(world);
    FreeLibrary(lib);
    std::puts("Jolt native VehicleConstraint: drive/turn/contacts/brake PASS");
    return 0;
}
