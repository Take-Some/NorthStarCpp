#include <northstarcpp/physics.hpp>
#include <northstarcpp/scene.hpp>
#include "../engine/northstarcpp/modules/host/vehicle_rig_xml.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
int main(int argc,char** argv) {
 try {
  if(argc!=3)throw std::invalid_argument("Usage: scene.nscn rig.nvehicle");
  auto scene=northstarcpp::load_scene(argv[1]);
  auto it=std::find_if(scene.objects.begin(),scene.objects.end(),
    [](auto const& x){return x.name=="Mahir MT28 - Native Vehicle";});
  if(it==scene.objects.end())throw std::runtime_error("Missing MT28");
  std::size_t const index=std::size_t(it-scene.objects.begin());
  std::ifstream stream(argv[2],std::ios::binary);
  if(!stream)throw std::runtime_error("Missing MT28 rig XML");
  std::string data((std::istreambuf_iterator<char>(stream)),
                    std::istreambuf_iterator<char>());
  auto rig=northstarcpp::parse_vehicle_rig_xml(data);
  if(rig.contact_mode!="physics"||rig.wheels.size()!=8)
   throw std::runtime_error("Invalid MT28 physical rig");
  northstarcpp::PhysicsScene physics(scene);
  if(physics.provider_name()!="Jolt"||!physics.supports_native_vehicle())
   throw std::runtime_error("Native Jolt VehicleConstraint not selected");
  physics.create_native_vehicle(index,rig.view(),3.14159265f);
  if(physics.native_vehicle_wheel_count(index)!=8)
   throw std::runtime_error("Vehicle has wrong wheel count");
  auto initial=physics.native_vehicle_pose(index);
  physics.native_vehicle_controls(index,.8f,0.f,0.f);
  float peak=0.f;
  unsigned touched=0;
  for(int n=0;n<500;++n){
   physics.step(1.f/60.f,0.f,0.f);
   auto pose=physics.native_vehicle_pose(index);
   float speed=std::hypot(pose.linear_velocity[0],pose.linear_velocity[2]);
   if(!std::isfinite(speed)||!std::isfinite(pose.y))
    throw std::runtime_error("Vehicle integration became nonfinite");
   peak=std::max(peak,speed);
   for(unsigned w=0;w<8;++w)
    touched+=physics.native_vehicle_wheel(index,w).in_contact;
  }
  auto final=physics.native_vehicle_pose(index);
  if(touched==0||peak<.25f)
   throw std::runtime_error("No ground contact or propulsion");
  bool refused=false;
  try {physics.move_static(index,{final.x,final.y,final.z});}
  catch(std::exception const&){refused=true;}
  if(!refused)throw std::runtime_error("Dynamic chassis accepted static teleport");
  // Dynamic obstacle is pushed by the Jolt solver, not by kinematic teleport.
  auto obstacle=scene.objects[index];
  obstacle.name="QA collision target";
  obstacle.node.id=990001;
  obstacle.node.parent=0;
  obstacle.node.asset.clear();
  obstacle.node.collision=true;
  // The obstacle is a non-Pawn Scene Entity.
  obstacle.mass=120.f;
  obstacle.size=1.3f;
  obstacle.position={final.x,33.55f,final.z-13.f};
  auto const obstacle_index=physics.spawn_runtime(obstacle);
  auto const obstacle_start=physics.scene().objects[obstacle_index].position;
  physics.native_vehicle_controls(index,.9f,0.f,0.f);
  for(int n=0;n<165;++n)physics.step(1.f/60.f,0.f,0.f);
  auto const obstacle_end=physics.scene().objects[obstacle_index].position;
  float const impact_distance=std::hypot(
      obstacle_end.x-obstacle_start.x,obstacle_end.z-obstacle_start.z);
  if(impact_distance<.10f)
    throw std::runtime_error("Jolt chassis did not transfer impact to dynamic obstacle");
  physics.native_vehicle_controls(index,0.f,0.f,1.f);
  for(int n=0;n<180;++n)physics.step(1.f/60.f,0.f,0.f);
  physics.destroy_native_vehicle(index);
  physics.move_static(index,scene.objects[index].position);
  std::cout<<"Jolt MT28 world PASS wheels=8 contacts="<<touched
           <<" peak_mps="<<peak<<" obstacle_move_m="<<impact_distance<<" drive_m="<<std::hypot(final.x-initial.x,
           final.z-initial.z)<<"\n";
  return 0;
 }catch(std::exception const& e){std::cerr<<e.what()<<"\n";return 2;}
}
