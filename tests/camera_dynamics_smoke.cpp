// NorthStarCpp independent camera dynamics acceptance tests.
// Checks exact second-order integration rather than a frame-dependent lerp.
#include <northstarcpp/camera_dynamics.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace northstarcpp;
namespace dyn=northstarcpp::camera_dynamics;
static void check(bool valid,char const* reason) {
    if(!valid)throw std::runtime_error(reason);
}
static float error(Vector3 a,Vector3 b) {
    return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);
}
int main() {
    try {
        Vector3 const origin{0.f,1.f,0.f};
        Vector3 const goal{15.f,3.f,-8.f};
        std::cout<<"INIT "<<origin.x<<" "<<origin.y<<" "<<origin.z
            <<" GOAL "<<goal.x<<" "<<goal.y<<" "<<goal.z<<"\n";
        for(float damping:{.35f,1.f,2.5f}) {
            dyn::SpringState thirty,one_twenty;
            auto low=origin,high=origin;
            auto const coeff=dyn::response(1.f/30.f,3.f,damping);
            std::cout<<"COEFF "<<coeff.position_from_offset<<" "
                     <<coeff.position_from_velocity<<" "
                     <<coeff.velocity_from_offset<<" "
                     <<coeff.velocity_from_velocity<<"\n";
            for(int step=0;step<30;++step) {
                low=dyn::advance(low,goal,thirty,1.f/30.f,3.f,damping);
                if(step<3) std::cout<<"LOW "<<step<<" "
                    <<low.x<<" "<<low.y<<" "<<low.z<<" v "
                    <<thirty.velocity.x<<"\n";
            }
            for(int step=0;step<120;++step)
                high=dyn::advance(high,goal,one_twenty,1.f/120.f,3.f,damping);
            std::cout<<"damping="<<damping<<" position_delta="
                     <<error(low,high)<<" velocity_delta="
                     <<error(thirty.velocity,one_twenty.velocity)<<"\n";
            check(error(low,high)<.00005f,
                "Damped spring depends on update frame rate");
            check(error(thirty.velocity,one_twenty.velocity)<.0001f,
                "Damped spring velocity depends on update frame rate");
            check(std::isfinite(low.x)&&std::isfinite(low.y)&&
                  std::isfinite(low.z),
                "Damped spring generated nonfinite camera coordinates");
        }
        // Critically damped spring may not overshoot a resting target.
        dyn::SpringState critical;
        auto p=origin;
        for(int frame=0;frame<120;++frame) {
            p=dyn::advance(p,goal,critical,1.f/120.f,2.5f,1.f);
            check(p.x<=goal.x+.0001f,"Critical damping overshot target");
        }
        check(error(p,goal)<.0002f,"Critical damping failed to converge");
        dyn::SpringState snap;
        snap.velocity={22.f,10.f,0.f};
        auto immediate=dyn::advance(origin,goal,snap,.016f,0.f,1.f);
        check(error(immediate,goal)==0.f && error(snap.velocity,{})==0.f,
              "Disabled spring failed to cut to authoritative camera pose");
        check(std::abs(dyn::exponential_alpha(.12f,.12f)-
                       (1.f-std::exp(-1.f)))<1.e-6f,
              "First-order smoothing response incorrect");
        std::cout<<"Camera exact spring dynamics: PASS\n";
        return 0;
    }catch(std::exception const& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
