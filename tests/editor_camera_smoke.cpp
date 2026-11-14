// Editor camera/picking regression — provider-independent.
#include "editor_camera.hpp"
#include "editor_tools.hpp"
#include <cmath>
#include <limits>
int main() {
    using namespace northstarcpp;
    using namespace northstarcpp::editor_tools;
    EditorCamera c;
    c.pose.position={0.f,1.f,-10.f};
    c.pose.yaw=0.f;c.pose.pitch=0.f;
    auto projected=c.project({0.f,1.f,0.f},1280.f,720.f);
    if(!projected || std::abs(projected->x-640.f)>1e-3f ||
        std::abs(projected->y-360.f)>1e-3f)return 1;
    auto ray=c.screen_ray(640.f,360.f,1280.f,720.f);
    if(std::abs(ray.direction.z-1.f)>1e-4f)return 2;
    Scene scene;
    scene.objects.resize(2);
    scene.objects[0].position={0.f,0.5f,5.f};
    scene.objects[0].size=1.f;
    scene.objects[1].position={0.f,0.5f,0.f};
    scene.objects[1].size=1.f;
    if(pick_actor(scene,ray)!=1)return 3; // nearer actor
    scene.version=3;scene.objects[1].node.visible=false;
    if(pick_actor(scene,ray)!=0)return 4;
    scene.objects[0].node.rotation.y=45.f;
    if(!intersect_actor_proxy(ray,scene.objects[0],3))return 5;
    c.look(0.f,-10000.f);
    if(c.pose.pitch>1.52001f)return 6;
    c.look(0.f,10000.f);
    if(c.pose.pitch< -1.52001f)return 7;
    c.pose.pitch=0.f;c.frame({0.f,0.f,0.f},10.f);
    if(std::abs(c.pose.position.z+10.f)>1e-4f)return 8;
    c.pose.position={0.f,0.f,0.f};
    c.pose.pitch=.5f;
    c.fly(0.f,3.f,0.f);
    if(std::abs(c.pose.position.y-3.f)>1e-5f ||
       std::abs(c.pose.position.z)>1e-5f)return 9;
    c.fly(2.f,0.f,0.f);
    if(std::abs(c.pose.position.x-2.f)>1e-5f ||
       std::abs(c.pose.position.y-3.f)>1e-5f)return 10;
    c.fly(0.f,0.f,5.f);
    if(c.pose.position.y<=3.f || c.pose.position.z<=0.f)return 11;
    auto const old=c.pose.position;
    c.fly(std::numeric_limits<float>::infinity(),0.f,0.f);
    if(c.pose.position.x!=old.x || c.pose.position.y!=old.y ||
       c.pose.position.z!=old.z)return 12;
    return 0;
}
