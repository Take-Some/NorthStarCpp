#include <northstarcpp/character_facing.hpp>
#include <northstarcpp/math.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
constexpr float pi=3.14159265358979323846f;
bool near(float a,float b,float eps=0.04f) {
    return std::abs(std::remainder(a-b,2.f*pi))<eps;
}
void require(bool condition,char const* why) {
    if(!condition)throw std::runtime_error(why);
}
template<typename Fn> void must_reject(Fn const& fn) {
    bool threw=false;
    try{fn();}catch(std::invalid_argument const&){threw=true;}
    require(threw,"Invalid character rotation input was accepted");
}
void turn_towards(northstarcpp::CharacterFacing& facing,float dx,float dz) {
    for(unsigned frame=0;frame<75;++frame)
        facing.advance(dx/60.f,dz/60.f,1.f/60.f);
}
} // namespace

int main() {
    try {
        using northstarcpp::CharacterFacing;
        CharacterFacing face;
        require(near(face.heading(),0.f),"Initial forward must be +Z");
        turn_towards(face,0,3);
        require(near(face.heading(),0.f),"W forward changed heading");
        turn_towards(face,3,0);
        require(near(face.heading(),pi/2.f),"D strafe did not face +X");
        turn_towards(face,-3,0);
        require(near(std::abs(face.heading()),pi/2.f),"A strafe did not face -X");
        turn_towards(face,0,-3);
        require(near(std::abs(face.heading()),pi),"S backward did not face -Z");
        turn_towards(face,3,3);
        require(near(face.heading(),pi/4.f),"Diagonal movement did not face diagonal");
        float const paused=face.heading();
        for(unsigned i=0;i<200;++i)
            face.advance(0,0,1.f/120.f);
        require(near(face.heading(),paused,0.0001f),"Idle rotated with the camera");
        // Shortest arc across +/-PI: must not spin through forward direction.
        face.reset(pi-0.08f);
        auto result=face.advance(-.01f,-3.f,1.f/60.f);
        require(result>pi-0.09f || result< -pi+0.1f,
                "Facing did not use shortest rotation through the seam");
        auto const saved=face.heading();
        require(near(face.advance(0,0,0.f),saved,0.0001f),
                "Paused frame changed character rotation");
        require(near(face.advance(0.001f,0.001f,0.016f),saved,0.0001f),
                "Resting physics drift changed heading");
        require(near(face.advance(100.f,100.f,0.016f),saved,0.0001f),
                "Teleport changed heading");
        face.reset(0.f);
        auto const first=face.advance(3.f/60.f,0.f,1.f/60.f);
        require(first>0.f && first<pi/2.f,"Rotation snapped instead of blending");
        // Yaw convention remains aligned with the real FPS movement system.
        auto const direction=northstarcpp::math::create_rotation_y(-pi/2.f)
            .transform_direction({0.f,0.f,1.f});
        require(std::abs(direction.x-1.f)<0.0001f &&
                std::abs(direction.z)<0.0001f,
                "Character model faces away from +X walking direction");
        auto const backward=northstarcpp::math::create_rotation_y(-pi)
            .transform_direction({0.f,0.f,1.f});
        require(std::abs(backward.z+1.f)<0.0001f,
                "Backward movement does not point character to -Z");
        must_reject([&]{face.reset(std::numeric_limits<float>::quiet_NaN());});
        must_reject([&]{face.advance(1,0,-0.1f);});
        must_reject([&]{face.advance(1,0,0.3f);});
        must_reject([&]{face.advance(std::numeric_limits<float>::infinity(),0,0.016f);});
        std::cout<<"Character movement-facing, stop, wrap and camera independence PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Character facing regression: "<<e.what()<<'\n';
        return 1;
    }
}
