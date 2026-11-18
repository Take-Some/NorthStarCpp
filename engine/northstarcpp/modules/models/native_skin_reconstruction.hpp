// NorthStarCpp — fallback for NVRM source skin streams with *unmapped*
// palette IDs. Used only when the source explicitly marks them inert.
//
// Native PAK streams may use per-part 10-bit skin IDs; those IDs are NOT
// necessarily global YMT indices. Directly applying them creates stretched
// faces/hair/shoes. This bounded approximation remaps each per-part source ID
// by the weighted center of the vertices it influences, onto canonical rig
// anchors in the YMT bind pose. It preserves source vertex weight magnitudes,
// original mesh topology, normals, materials, UVs, hierarchy and animations.
//
// IMPORTANT: Heuristic deformation is not a replacement for source-native
// bone-palette decoding. Do not silently label these weights source-authored.
#pragma once
#include <northstarcpp/character.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace northstarcpp::native_skin_reconstruction {
struct Report {
    std::size_t source_ids=0;
    std::size_t corrected_influences=0;
    std::size_t corrected_vertices=0;
    float maximum_centroid_distance=0.f;
};
struct Anchor {
    std::uint16_t id=0;
    math::float3 position{0.f,0.f,0.f};
};
inline std::vector<Anchor> canonical_anchors(
    Skeleton const& rig,math::float4x4 const& mesh_to_skeleton)
{
    // A small, stable set of deforming bones. Facial/hair/cloth attachments
    // remain attached to the nearest head/torso anchor until their original
    // extended skeleton palette has been decoded and verified.
    constexpr std::string_view names[]{
        "root","pelvis","spinea","spineb","spinec","spined",
        "neck","heada","headb",
        "l_clavicle","r_clavicle",
        "l_shoulder","r_shoulder","l_elbow","r_elbow",
        "l_wrist","r_wrist","l_palm","r_palm",
        "l_upper_leg","r_upper_leg","l_knee","r_knee",
        "l_ankle","r_ankle","l_ball","r_ball",
        "l_toe","r_toe"
    };
    math::float4x4 skeleton_to_mesh;
    if(!skeleton_to_mesh.try_invert(mesh_to_skeleton))
        throw std::invalid_argument("Non-invertible inert skin reference basis");
    std::vector<Anchor> result;
    result.reserve(std::size(names));
    for(auto const name:names) {
        const auto joint=rig.joint_index(name);
        if(joint>=rig.joints.size() || joint>=UINT16_MAX)continue;
        math::float4x4 global;
        if(!global.try_invert(rig.inverse_bind[joint]))
            throw std::invalid_argument("Invalid inert skin bind matrix");
        auto const pivot=skeleton_to_mesh.transform_position(
            global.transform_position({0.f,0.f,0.f}));
        if(!std::isfinite(pivot.x)||!std::isfinite(pivot.y)||
           !std::isfinite(pivot.z))continue;
        result.push_back({static_cast<std::uint16_t>(joint),pivot});
    }
    if(result.size()<16)
        throw std::invalid_argument(
            "Inert source skin has no sufficient humanoid rig anchors");
    return result;
}
inline float distance_squared(math::float3 a,math::float3 b) {
    auto const x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;
    return x*x+y*y+z*z;
}
inline std::uint16_t nearest_anchor(
    math::float3 point,std::vector<Anchor> const& anchors)
{
    auto best=anchors.front().id;
    float score=std::numeric_limits<float>::infinity();
    for(auto const& anchor:anchors) {
        const auto distance=distance_squared(point,anchor.position);
        if(distance<score) {
            score=distance;
            best=anchor.id;
        }
    }
    return best;
}
inline Report rebind_part(
    CharacterMesh& part,Skeleton const& rig,
    std::vector<Anchor> const& anchors)
{
    if(part.mesh.vertices.size()!=part.skin.size() || part.skin.empty())
        throw std::invalid_argument("Invalid inert source skin data");
    struct Centroid {
        double x=0,y=0,z=0,weight=0;
    };
    std::unordered_map<std::uint16_t,Centroid> groups;
    groups.reserve(128);
    // Each source ID is scoped per NVRM submesh, NEVER across materials.
    for(std::size_t i=0;i<part.skin.size();++i) {
        auto const& skin=part.skin[i];
        auto const& p=part.mesh.vertices[i].position;
        for(unsigned k=0;k<8;++k) {
            const auto w=skin.weights[k];
            if(w<=0.f)continue;
            auto& c=groups[skin.joints[k]];
            c.x+=double(p[0])*w;
            c.y+=double(p[1])*w;
            c.z+=double(p[2])*w;
            c.weight+=w;
        }
    }
    std::unordered_map<std::uint16_t,std::uint16_t> mapped;
    mapped.reserve(groups.size());
    Report report;
    report.source_ids=groups.size();
    for(auto const& [id,c]:groups) {
        if(c.weight<=0.)continue;
        const auto centroid=math::float3{
            float(c.x/c.weight),float(c.y/c.weight),float(c.z/c.weight)};
        mapped.emplace(id,nearest_anchor(centroid,anchors));
        auto nearest=std::find_if(anchors.begin(),anchors.end(),
            [&](Anchor const& candidate) {return candidate.id==mapped.at(id);});
        if(nearest!=anchors.end())
            report.maximum_centroid_distance=std::max(
                report.maximum_centroid_distance,
                std::sqrt(distance_squared(centroid,nearest->position)));
    }
    // A single ID may be reused in widely separated source data. Correct
    // influences falling further than 60 cm from the inferred joint using
    // a per-vertex fallback. This avoids hair moving with a foot even if
    // some PAK submesh reuses another local palette entry.
    constexpr float max_anchor_distance_sq=.60f*.60f;
    for(std::size_t i=0;i<part.skin.size();++i) {
        auto& skin=part.skin[i];
        auto const& p=part.mesh.vertices[i].position;
        const math::float3 point{p[0],p[1],p[2]};
        const auto fallback=nearest_anchor(point,anchors);
        bool any=false;
        for(unsigned k=0;k<8;++k) {
            if(skin.weights[k]<=0.f)continue;
            const auto original=skin.joints[k];
            auto proposed=mapped.at(original);
            auto found=std::find_if(anchors.begin(),anchors.end(),
                [&](Anchor const& candidate){return candidate.id==proposed;});
            if(found==anchors.end() ||
               distance_squared(point,found->position)>max_anchor_distance_sq)
                proposed=fallback;
            if(proposed!=original) {
                ++report.corrected_influences;
                any=true;
            }
            skin.joints[k]=proposed;
        }
        report.corrected_vertices+=any?1:0;
    }
    return report;
}
} // namespace northstarcpp::native_skin_reconstruction
