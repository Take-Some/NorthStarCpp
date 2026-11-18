#include "../engine/northstarcpp/modules/viewport/viewport_skeletal_basis.hpp"
#include <cmath>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(false)
int main() {
    using namespace northstarcpp;
    math::float4x4 imported;
    imported.identity();
    // Authored YDD->YMT skeletal basis is a proper half-turn around Y.
    imported.elements[0][0]=-1.f;
    imported.elements[2][2]=-1.f;
    auto const display=viewport_detail::skeletal_presentation_basis(imported);
    auto const eye_mesh=math::float3{0.f,1.62f,-.108f};
    auto const toe_mesh=math::float3{.146f,.002f,-.19f};
    auto const face_world=display.transform_position(eye_mesh);
    auto const foot_world=display.transform_position(toe_mesh);
    CHECK(face_world.z>0.10f && foot_world.z>0.18f);
    CHECK(std::abs(face_world.y-eye_mesh.y)<1.e-5f);
    // Animation binds and socket positions use the same world conversion:
    // p_mesh * M * P * inverse(M) * (M * W) = p_mesh * M * P * W.
    auto const rebuilt=(imported*display).transform_position(eye_mesh);
    CHECK(std::abs(rebuilt.z-eye_mesh.z)<1.e-5f);
    math::float4x4 identity;identity.identity();
    auto unchanged=viewport_detail::skeletal_presentation_basis(identity);
    CHECK(std::abs(unchanged.transform_position(toe_mesh).z-toe_mesh.z)<1.e-5f);
    // Regression: the CPU material/triangle path uses cube.rotation while
    // the GPU palette and animated sockets use the 4x4 mesh world matrix.
    // A 180-degree imported basis must yield IDENTICAL positions in both.
    math::float4x4 authored_world;authored_world.identity();
    const float size=1.25f;
    for(int axis=0;axis<3;++axis)
        authored_world.elements[axis][axis]=size;
    authored_world.elements[3][0]=3.f;
    authored_world.elements[3][1]=1.f;
    authored_world.elements[3][2]=-2.f;
    auto const presented_world=display*authored_world;
    NorthStarRenderCube cube{};
    cube.x=3.f;cube.y=1.f;cube.z=-2.f;
    viewport_detail::synchronize_skeletal_render_cube(
        cube,presented_world,size);
    for(auto point: {eye_mesh,toe_mesh,math::float3{.26f,1.18f,-.41f}}) {
        auto const gpu=presented_world.transform_position(point);
        const math::float3 cpu{
            cube.x+size*(point.x*cube.rotation[0]+point.y*cube.rotation[1]+point.z*cube.rotation[2]),
            cube.y+size*(point.x*cube.rotation[3]+point.y*cube.rotation[4]+point.z*cube.rotation[5]),
            cube.z+size*(point.x*cube.rotation[6]+point.y*cube.rotation[7]+point.z*cube.rotation[8])};
        CHECK((cpu-gpu).magnitude()<1.e-5f);
    }
    // The old unsynchronized cube would have placed the same hand almost
    // a metre away from the YMT socket after the rig presentation half-turn.
    auto const wrist=math::float3{.26f,1.18f,-.41f};
    auto const socket=presented_world.transform_position(wrist);
    auto const old_draw=authored_world.transform_position(wrist);
    CHECK((socket-old_draw).magnitude()>.70f);
    auto invalid=identity;
    invalid.elements[0][0]=-1.f; // Mirroring is not a valid facing rotation.
    auto rejected=viewport_detail::skeletal_presentation_basis(invalid);
    CHECK(std::abs(rejected.transform_position(toe_mesh).z-toe_mesh.z)<1.e-5f);
}
