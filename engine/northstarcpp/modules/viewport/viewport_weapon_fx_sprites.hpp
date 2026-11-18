#pragma once
// Provider-neutral textured weapon particles. These sprites use the existing
// Materials V2 alpha-blend API on D3D12/D3D11/Vulkan, not opaque scene cubes.
// Each particle has independent motion/orientation and a per-vertex opacity
// choice encoded in one shared RGBA atlas; only three material batches per
// frame are submitted, regardless of particle count.
#include "viewport_frame_scratch.hpp"
#include <northstarcpp/fps_weapon_effects.hpp>
#include <northstarcpp/camera.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace northstarcpp::viewport_detail {
inline auto const& firearm_sprite_atlases() {
    constexpr int side=96;
    constexpr int alpha_levels=6;
    using Pixels=std::array<std::uint8_t,side*side*alpha_levels*4>;
    static const std::array<Pixels,3> atlas=[]{
        std::array<Pixels,3> images{};
        constexpr float alphas[alpha_levels]={.12f,.24f,.40f,.58f,.78f,1.f};
        for(int kind=0;kind<3;++kind)
            for(int a=0;a<alpha_levels;++a)
                for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
                    const auto u=(float(x)+.5f)/float(side)*2.f-1.f;
                    const auto v=(float(y)+.5f)/float(side)*2.f-1.f;
                    const auto radial=u*u+v*v;
                    float mask=0.f;
                    if(kind==0) {
                        // An incandescent center with irregular star-shaped rays.
                        const auto core=std::exp(-7.4f*radial);
                        const auto cross=std::max(
                            std::exp(-115.f*v*v-3.f*u*u),
                            std::exp(-115.f*u*u-3.f*v*v));
                        const auto diagonal=std::exp(
                            -54.f*(u-v)*(u-v)-6.f*(u+v)*(u+v));
                        mask=std::min(1.f,core*.9f+cross*.30f+
                            diagonal*.10f);
                    } else if(kind==1) {
                        // An elongated hot filament with a feathered tail.
                        mask=std::exp(-115.f*u*u-2.7f*v*v);
                    } else {
                        // Low-frequency curl and erosion: layered wisps that
                        // grow translucent at their perimeter, never gray discs.
                        const float curl=std::sin(6.7f*u+5.2f*v)*
                            std::sin(6.1f*v-2.5f*u);
                        const float folds=std::sin(10.1f*u-3.7f*v)+
                            std::sin(9.3f*v+2.8f*u);
                        const float density=std::clamp(
                            .63f+.18f*curl+.085f*folds,.08f,1.f);
                        mask=std::exp(-3.6f*radial)*density;
                    }
                    // The texture includes an actual transparent border.
                    mask*=std::clamp((1.f-radial)*7.f,0.f,1.f);
                    const auto idx=(y*(side*alpha_levels)+a*side+x)*4;
                    images[kind][idx+0]=kind==2?170u:255u;
                    images[kind][idx+1]=kind==2?173u:kind==1?189u:226u;
                    images[kind][idx+2]=kind==2?182u:kind==1?96u:174u;
                    const auto maxAlpha=kind==2?.72f:1.f;
                    images[kind][idx+3]=static_cast<std::uint8_t>(
                        std::clamp(mask*alphas[a]*maxAlpha*255.f,0.f,255.f));
                }
        return images;
    }();
    return atlas;
}
inline std::size_t append_weapon_sprites(
    FpsWeaponEffects const& fx,CameraPose const& camera,
    ViewportFrameScratch& scratch,
    std::vector<NorthStarMaterialBatch>& materials,
    std::vector<NorthStarMaterialBatchV2>& variants,
    std::vector<char const*>& keys) {
    constexpr float alpha_levels=6.f;
    const auto sy=std::sin(camera.yaw),cy=std::cos(camera.yaw);
    const auto sp=std::sin(camera.pitch),cp=std::cos(camera.pitch);
    const Vector3 cameraRight{cy,0.f,-sy};
    const Vector3 cameraUp{-sy*sp,cp,-cy*sp};
    const Vector3 normal{-sy*cp,-sp,-cy*cp};
    auto dot=[](Vector3 a,Vector3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    auto point=[](Vector3 a,Vector3 u,float s,Vector3 v,float t) {
        return Vector3{a.x+u.x*s+v.x*t,
            a.y+u.y*s+v.y*t,a.z+u.z*s+v.z*t};
    };
    for(auto const& effect:fx.visuals()) {
        const auto kind=std::clamp(int(effect.kind),0,2);
        const auto alpha=std::clamp(effect.opacity,0.f,1.f);
        if(alpha<.015f)continue;
        const auto band=std::clamp(int(alpha*alpha_levels),0,5);
        const auto u0=float(band)/alpha_levels;
        const auto u1=float(band+1)/alpha_levels;
        auto& geometry=scratch.weapon_fx_vertices[kind];
        const auto w=std::clamp(effect.scale.x*1.75f,.003f,.65f);
        const auto h=std::clamp(effect.scale.y*1.75f,.005f,.65f);
        Vector3 right{},up{};
        if(kind==1) {
            // Sparks are elongated ALONG projected velocity, including upward
            // or diagonal trajectories. No flat horizontal painted streaks.
            const auto vx=dot(effect.motion,cameraRight);
            const auto vy=dot(effect.motion,cameraUp);
            const auto magnitude=std::sqrt(vx*vx+vy*vy);
            if(magnitude>.01f) {
                up=Vector3{cameraRight.x*vx/magnitude+
                    cameraUp.x*vy/magnitude,
                    cameraRight.y*vx/magnitude+
                    cameraUp.y*vy/magnitude,
                    cameraRight.z*vx/magnitude+
                    cameraUp.z*vy/magnitude};
                right=Vector3{cameraRight.x*vy/magnitude-
                    cameraUp.x*vx/magnitude,
                    cameraRight.y*vy/magnitude-
                    cameraUp.y*vx/magnitude,
                    cameraRight.z*vy/magnitude-
                    cameraUp.z*vx/magnitude};
            } else {
                up=cameraUp;right=cameraRight;
            }
        } else {
            // Individually rotated smoke prevents a row of identical puffs.
            const auto c=std::cos(effect.rotation),s=std::sin(effect.rotation);
            right=Vector3{cameraRight.x*c+cameraUp.x*s,
                cameraRight.y*c+cameraUp.y*s,
                cameraRight.z*c+cameraUp.z*s};
            up=Vector3{cameraUp.x*c-cameraRight.x*s,
                cameraUp.y*c-cameraRight.y*s,
                cameraUp.z*c-cameraRight.z*s};
        }
        const auto a=point(effect.position,right,-w*.5f,up,h*.5f);
        const auto b=point(effect.position,right,w*.5f,up,h*.5f);
        const auto c=point(effect.position,right,-w*.5f,up,-h*.5f);
        const auto d=point(effect.position,right,w*.5f,up,-h*.5f);
        auto vertex=[&](Vector3 p,float u,float v){
            return NorthStarMaterialVertex{p.x,p.y,p.z,
                normal.x,normal.y,normal.z,u,v};
        };
        geometry.push_back(vertex(a,u0,0.f));
        geometry.push_back(vertex(c,u0,1.f));
        geometry.push_back(vertex(b,u1,0.f));
        geometry.push_back(vertex(b,u1,0.f));
        geometry.push_back(vertex(c,u0,1.f));
        geometry.push_back(vertex(d,u1,1.f));
    }
    auto const& atlases=firearm_sprite_atlases();
    char const* const names[]={"NorthStar/weapon/incandescence",
        "NorthStar/weapon/ballistic_sparks","NorthStar/weapon/volumetric_smoke"};
    std::size_t count=0;
    for(int kind=0;kind<3;++kind) {
        auto& geometry=scratch.weapon_fx_vertices[kind];
        // Submit a stable, non-visible degenerate triangle even when this
        // emitter has no live particles. Otherwise the first shot adds three
        // brand-new material batches, forcing Vulkan texture uploads, PSO
        // compilation and vkDeviceWaitIdle on the exact frame of firing.
        if(geometry.empty()) {
            const Vector3 behind{
                camera.position.x+normal.x*.25f,
                camera.position.y+normal.y*.25f,
                camera.position.z+normal.z*.25f};
            for(int i=0;i<3;++i)
                geometry.push_back(NorthStarMaterialVertex{
                    behind.x,behind.y,behind.z,
                    normal.x,normal.y,normal.z,0.f,0.f});
        }
        NorthStarMaterialBatch batch{};
        batch.vertices=geometry.data();
        batch.vertex_count=static_cast<std::uint32_t>(geometry.size());
        batch.base_color[0]=batch.base_color[1]=batch.base_color[2]=1.f;
        batch.base_color[3]=1.f;
        batch.metallic=0.f;batch.roughness=1.f;batch.unlit=1u;
        batch.textures[0]={atlases[kind].data(),576u,96u,
            static_cast<std::uint32_t>(atlases[kind].size())};
        NorthStarMaterialBatchV2 variant{};
        variant.material=batch;
        variant.blend_mode=2u;
        variant.opacity=1.f;
        materials.push_back(batch);
        variants.push_back(variant);
        keys.push_back(names[kind]);
        ++count;
    }
    return count;
}
} // namespace northstarcpp::viewport_detail
