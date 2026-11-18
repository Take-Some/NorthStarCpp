#include "../engine/northstarcpp/modules/api/include/northstarcpp/camera_owner_clearance.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
struct V { float x,y,z; };
void require(bool condition,const char* why) {if(!condition)throw std::runtime_error(why);}
int main() {
 using namespace northstarcpp::camera_collision;
 V feet{0,0,0};
 auto clear=[](V){return true;};
 auto camera=V{0,1.55f,0};
 auto fps=keep_camera_outside_player(camera,feet,1.55f,0.f,first_person_head_radius,true,clear);
 require(!player_camera_inside_capsule(fps,feet,1.55f,first_person_head_radius),"FPS camera intersects head");
 require(fps.z>.20f && fps.z<.30f,"FPS camera must be placed in front, not through the face");
 auto tps=keep_camera_outside_player(camera,feet,1.55f,0.f,.77f,false,clear);
 require(!player_camera_inside_capsule(tps,feet,1.55f,.77f),"TPS camera intersects head");
 require(tps.z<-.7f,"TPS camera must be behind");
 V far{0,1.55f,-3.f};
 auto stable=keep_camera_outside_player(far,feet,1.55f,0.f,.77f,false,clear);
 require(player_camera_distance_squared(far,stable)<1e-6f,"unobstructed camera moved");
 auto left=keep_camera_outside_player(camera,feet,1.55f,0.f,.77f,false,
     [](V candidate){return candidate.x < -.70f;});
 require(!player_camera_inside_capsule(left,feet,1.55f,.77f),
         "shoulder alternative must remain outside");
 require(left.x<-.70f,"shoulder alternative not used");
 auto impossible=keep_camera_outside_player(camera,feet,1.55f,0.f,.77f,false,
     [](V){return false;});
 require(player_camera_distance_squared(impossible,camera)<1e-6f,
         "blocked physics must not teleport camera into walls");
 std::cout<<"Camera owner capsule clearance PASS"<<std::endl;
}
