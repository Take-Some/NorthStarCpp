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

    // The same one-second flight must be frame-rate-independent, including
    // input acceleration and exact displacement integration.
    EditorCamera flight30,flight120;
    flight30.pose.position={0.f,0.f,0.f};
    flight120.pose.position=flight30.pose.position;
    flight30.pose.yaw=flight120.pose.yaw=0.f;
    flight30.pose.pitch=flight120.pose.pitch=0.f;
    for(int i=0;i<30;++i)
        if(!flight30.fly_damped(1.f,0.f,1.f,12.f,1.f/30.f))return 13;
    for(int i=0;i<120;++i)
        if(!flight120.fly_damped(1.f,0.f,1.f,12.f,1.f/120.f))return 14;
    if(std::abs(flight30.pose.position.x-flight120.pose.position.x)>1.e-3f ||
       std::abs(flight30.pose.position.z-flight120.pose.position.z)>1.e-3f)
        return 15;
    // Both direction axes must remain normalized; diagonal fly must not
    // move sqrt(2) faster than straight forward movement.
    if(std::hypot(flight30.flight_velocity().x,
                  flight30.flight_velocity().z)>12.001f)return 16;
    auto const before_braking=flight30.flight_velocity();
    flight30.fly_damped(0.f,0.f,0.f,12.f,1.f/60.f);
    if(!(flight30.flight_velocity().x<before_braking.x))return 17;
    for(int i=0;i<240;++i)
        flight30.fly_damped(0.f,0.f,0.f,12.f,1.f/120.f);
    if(std::hypot(flight30.flight_velocity().x,
                  flight30.flight_velocity().z)>1.e-3f)return 18;
    flight30.reset_flight();
    if(flight30.flight_velocity().x!=0.f ||
       flight30.flight_velocity().z!=0.f)return 19;
    auto const preserved=flight30.pose.position;
    if(flight30.fly_damped(std::numeric_limits<float>::infinity(),
                          0.f,1.f,12.f,.016f))return 20;
    if(flight30.pose.position.x!=preserved.x ||
       flight30.pose.position.z!=preserved.z)return 21;

    // Qt Viewport pointer regression: moving the pointer RIGHT turns
    // the camera RIGHT (yaw positive); moving UP raises pitch.
    // Both orbit and fly controllers use drag_look(), not raw look().
    EditorCamera mouse;
    mouse.pose.position={0.f,0.f,0.f};
    mouse.pose.yaw=0.f;mouse.pose.pitch=0.f;
    auto const center=mouse.project({0.f,0.f,10.f},1280.f,720.f);
    if(!center || std::abs(center->x-640.f)>1.e-3f ||
                  std::abs(center->y-360.f)>1.e-3f)return 22;
    mouse.drag_look(40.f,-40.f);
    if(mouse.pose.yaw<=0.f || mouse.pose.pitch<=0.f)return 23;
    auto const dragged=mouse.project({0.f,0.f,10.f},1280.f,720.f);
    if(!dragged || dragged->x>=center->x || dragged->y<=center->y)
        return 24;
    if(mouse.forward().x<=0.f || mouse.forward().y<=0.f)
        return 25;
    mouse.pose.yaw=mouse.pose.pitch=0.f;
    mouse.drag_look(-40.f,40.f);
    if(mouse.pose.yaw>=0.f || mouse.pose.pitch>=0.f)return 26;
    auto const opposite=mouse.project({0.f,0.f,10.f},1280.f,720.f);
    if(!opposite || opposite->x<=center->x || opposite->y>=center->y)
        return 27;
    mouse.pose.yaw=mouse.pose.pitch=0.f;
    mouse.drag_look(-100000.f,-100000.f,.006f);
    if(mouse.pose.pitch>1.52001f)return 28;
    mouse.drag_look(100000.f,100000.f,.006f);
    if(mouse.pose.pitch< -1.52001f)return 29;
    auto const unchanged=mouse.pose;
    mouse.drag_look(std::numeric_limits<float>::quiet_NaN(),1.f);
    if(mouse.pose.yaw!=unchanged.yaw || mouse.pose.pitch!=unchanged.pitch)
        return 30;
    return 0;
}
