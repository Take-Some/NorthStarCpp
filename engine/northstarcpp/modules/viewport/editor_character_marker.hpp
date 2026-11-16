// Editor-only capsule/heading marker for Pawns without a visual mesh.
// Uses the existing engine.render triangles ABI; never written to scene data.
#pragma once
#include <northstarcpp/scene.hpp>
#include <northstarcpp/render_triangles_api.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace northstarcpp::editor_character_marker {
inline constexpr unsigned slices=16;
inline constexpr unsigned rings=6;
inline constexpr std::size_t vertices_per_marker=(rings-1)*slices*6+3;
inline bool append(std::vector<NorthStarRenderTriangleVertex>& vertices,
                   SceneObject const& entity,bool selected,
                   std::size_t budget=300000) {
    if(vertices.size()>budget ||
       budget-vertices.size()<vertices_per_marker)return false;
    float const height=std::clamp(entity.size,1.6f,3.f);
    float const radius=std::clamp(height*.2f,.23f,.43f);
    std::array<float,6> const y{
        0.f,radius*.25f,radius,
        height-radius,height-radius*.25f,height};
    std::array<float,6> const rr{
        .001f,radius*.70f,radius,radius,radius*.70f,.001f};
    constexpr float tau=6.2831853071795864769f;
    constexpr float rad_per_degree=.01745329251994329577f;
    auto const yaw=entity.node.rotation.y*rad_per_degree;
    auto const cy=std::cos(yaw),sy=std::sin(yaw);
    auto point=[&](float x,float up,float z) {
        return Vector3{entity.position.x+cy*x+sy*z,
                       entity.position.y+up,
                       entity.position.z-sy*x+cy*z};
    };
    std::uint32_t const rgb=selected?0xffcb64u:0x48b8e4u;
    auto push=[&](Vector3 const& v,std::uint32_t c) {
        vertices.push_back({v.x,v.y,v.z,c});
    };
    for(unsigned ring=0;ring<rings-1;++ring)
        for(unsigned seg=0;seg<slices;++seg) {
            float const a=tau*float(seg)/float(slices);
            float const b=tau*float(seg+1)/float(slices);
            auto const p0=point(std::cos(a)*rr[ring],y[ring],
                                std::sin(a)*rr[ring]);
            auto const p1=point(std::cos(b)*rr[ring],y[ring],
                                std::sin(b)*rr[ring]);
            auto const p2=point(std::cos(a)*rr[ring+1],y[ring+1],
                                std::sin(a)*rr[ring+1]);
            auto const p3=point(std::cos(b)*rr[ring+1],y[ring+1],
                                std::sin(b)*rr[ring+1]);
            // Outward-facing winding; no debug mesh data survives the frame.
            push(p0,rgb);push(p2,rgb);push(p1,rgb);
            push(p1,rgb);push(p2,rgb);push(p3,rgb);
        }
    // Heading wedge disambiguates Pawn orientation in editor mode.
    float const h=height*.77f;
    push(point(-radius*.42f,h,radius*.98f),0xffce74u);
    push(point(radius*.42f,h,radius*.98f),0xffce74u);
    push(point(0.f,h,radius*1.85f),0xffce74u);
    return true;
}
} // namespace northstarcpp::editor_character_marker
