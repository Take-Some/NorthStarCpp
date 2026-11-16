#include <northstarcpp/vehicle_plugin.h>
#include <cmath>
#include <cstdio>
int main() {
  auto* m=northstar_module_query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_VEHICLE,NORTHSTARCPP_VEHICLE_ABI);
  if(!m || !m->api || m->api_size<sizeof(NorthStarVehicleApi))return 1;
  auto const& a=*static_cast<NorthStarVehicleApi const*>(m->api);
  auto* w=a.create_world(),*other=a.create_world();
  if(!w || !other)return 2;
  NorthStarVehicleDefinition d{};d.size=sizeof(d);d.entity_index=3;
  d.position[0]=847.5f;d.position[1]=33.7f;d.position[2]=2296.5f;
  d.heading=3.14159265f;d.wheelbase=6.2f;d.maximum_speed=18.f;
  d.acceleration=2.4f;d.braking=5.5f;d.maximum_steering=.51f;d.rolling_drag=.08f;
  if(!a.attach(w,&d)||a.attach(w,&d)||a.count(other)!=0)return 3;
  if(!a.waypoint(w,3,847.5f,2250.f,6.f,2.5f))return 4;
  for(int i=0;i<420;++i)if(!a.step(w,1.f/60.f))return 5;
  NorthStarVehicleState v{};v.size=sizeof(v);
  if(!a.read(w,3,&v)||v.position[2]>=2295.f||v.position[2]<=2240.f||v.speed<0)return 6;
  float const z=v.position[2];
  if(a.controls(w,3,2.f,0.f,0.f)||a.step(w,-.1f))return 7;
  v.size=sizeof(v);
  if(!a.read(w,3,&v)||v.position[2]!=z)return 8;
  if(!a.controls(w,3,0.f,0.f,1.f))return 9;
  for(int i=0;i<180;++i)if(!a.step(w,1.f/60.f))return 10;
  v.size=sizeof(v);
  if(!a.read(w,3,&v)||std::abs(v.speed)>.1f)return 11;
  NorthStarVehicleState e{};e.size=sizeof(e);
  if(!a.at(w,0,&e)||e.entity_index!=3||a.at(w,1,&e))return 12;
  if(!a.detach(w,3)||a.detach(w,3)||a.count(w)!=0)return 13;
  a.destroy_world(other);a.destroy_world(w);
  std::puts("engine.vehicle native ABI + dynamics: PASS");
  return 0;
}
