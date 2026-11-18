#pragma once
// Imported YDD mesh-local versus YMT skeleton-local orientation.
// The authored mesh_to_skeleton rotational basis is the canonical conversion;
// do not special-case character names, Pawn controllers, or camera providers.
#include <northstarcpp/skeleton.hpp>
#include <northstarcpp/render_plugin.h>
#include <cmath>

namespace northstarcpp::viewport_detail {
inline math::float4x4 skeletal_presentation_basis(
    math::float4x4 const& mesh_to_skeleton) noexcept {
    math::float4x4 identity;
    identity.identity();
    // Only a proper rigid rotation may be propagated to the character
    // presentation. Non-rigid model/rig offsets remain in the normal palette
    // conversion and must not distort the world transform.
    float det =
        mesh_to_skeleton.elements[0][0]*
            (mesh_to_skeleton.elements[1][1]*mesh_to_skeleton.elements[2][2]-
             mesh_to_skeleton.elements[1][2]*mesh_to_skeleton.elements[2][1])-
        mesh_to_skeleton.elements[0][1]*
            (mesh_to_skeleton.elements[1][0]*mesh_to_skeleton.elements[2][2]-
             mesh_to_skeleton.elements[1][2]*mesh_to_skeleton.elements[2][0])+
        mesh_to_skeleton.elements[0][2]*
            (mesh_to_skeleton.elements[1][0]*mesh_to_skeleton.elements[2][1]-
             mesh_to_skeleton.elements[1][1]*mesh_to_skeleton.elements[2][0]);
    if(!std::isfinite(det)||std::abs(det-1.f)>1.e-3f)
        return identity;
    for(int i=0;i<3;++i) {
        float norm=0.f;
        for(int j=0;j<3;++j)
            norm+=mesh_to_skeleton.elements[i][j]*mesh_to_skeleton.elements[i][j];
        if(!std::isfinite(norm)||std::abs(norm-1.f)>1.e-3f)
            return identity;
        for(int k=i+1;k<3;++k) {
            float dot=0.f;
            for(int j=0;j<3;++j)
                dot+=mesh_to_skeleton.elements[i][j]*mesh_to_skeleton.elements[k][j];
            if(std::abs(dot)>1.e-3f)return identity;
        }
    }
    for(int row=0;row<3;++row)
        for(int col=0;col<3;++col)
            identity.elements[row][col]=mesh_to_skeleton.elements[row][col];
    return identity;
}

// A skinned scene object's CPU triangles still use NorthStarRenderCube,
// while socket and GPU-skin pathways use mesh_world. Make the cube's 3x3
// match the *same* presented world matrix (row-vector math -> cube layout).
// Otherwise a 180-degree YDD/YMT presentation rotation places the weapon
// socket opposite the hand actually rendered by the CPU mesh path.
inline void synchronize_skeletal_render_cube(
    NorthStarRenderCube& cube,
    math::float4x4 const& presented_mesh_world,
    float object_size) noexcept {
    if(!std::isfinite(object_size)||object_size<=0.f)return;
    for(unsigned row=0;row<3;++row)
        for(unsigned col=0;col<3;++col)
            cube.rotation[row*3+col]=
                presented_mesh_world.elements[col][row]/object_size;
}
} // namespace northstarcpp::viewport_detail
