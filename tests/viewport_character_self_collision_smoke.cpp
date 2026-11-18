#include "../engine/northstarcpp/modules/viewport/viewport_character_self_collision.hpp"
#include <cmath>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(false)
using namespace northstarcpp;
using namespace northstarcpp::viewport_detail;
int main() {
    auto body=make_character_self_collision(
        {0,0,0},{0,.35f,0},{0,.60f,0},{0,.95f,0},
        {.20f,.61f,0},{-.20f,.61f,0},{0,0,1});
    CHECK(body.active && body.torso.radius>.22f);
    CHECK(body.correction({0,.40f,0},.04f)>.20f);
    const auto hand=body.outside({0,.4f,0},.04f);
    CHECK(body.correction(hand,.04f)<.002f);
    const auto look_down=Vector3{0,-.80f,.60f};
    auto const start=Vector3{0,.62f,.05f};
    const auto correction=body.weapon_correction(start,look_down,.28f,.04f);
    CHECK(correction>.05f);
    const auto safe=start+body.forward*correction;
    CHECK(body.weapon_correction(safe,look_down,.28f,.04f)<.003f);
    // Frame-camera yaw rotates the capsule clearance direction without
    // moving the authored torso out of its scene graph.
    auto sideways=make_character_self_collision(
        {0,0,0},{0,.35f,0},{0,.60f,0},{0,.95f,0},
        {.20f,.61f,0},{-.20f,.61f,0},{1,0,0});
    CHECK(sideways.active);
    auto const right_side=sideways.outside({0,.38f,0},.04f);
    CHECK(right_side.x>.20f && std::abs(right_side.z)<.001f);
    // Non-humanoid rigs are allowed: self collision stays disabled.
    auto invalid=make_character_self_collision(
        {},{},{},{},{0,0,0},{0,0,0},{0,0,1});
    CHECK(!invalid.active && invalid.correction({},.04f)==0.f);
}
