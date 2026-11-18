#include <northstarcpp/fps_weapon_effects.hpp>
#include <northstarcpp/physics.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace northstarcpp {
namespace {
constexpr float pi=3.14159265358979323846f;
bool finite(Vector3 v) {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
}
Vector3 normalize(Vector3 a) {
    auto const n=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);
    return n>.00001f?a/n:Vector3{0.f,0.f,1.f};
}
Vector3 cross(Vector3 a,Vector3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
float jitter(std::uint32_t seed) {
    seed^=seed>>16;seed*=0x7feb352du;
    seed^=seed>>15;seed*=0x846ca68bu;
    seed^=seed>>16;
    return float(seed&65535u)/65535.f*2.f-1.f;
}
float fadeout(float unit,float power) {
    auto const t=std::clamp(1.f-unit,0.f,1.f);
    return std::pow(t,power);
}
float smoothstep(float t) {
    t=std::clamp(t,0.f,1.f);
    return t*t*(3.f-2.f*t);
}
}
void FpsWeaponEffects::reset() {
    casings_.clear();
    bursts_.clear();
    visuals_.clear();
    rejected_casings_=0;
    sequence_=0;
}
void FpsWeaponEffects::spawn_burst(Vector3 muzzle,Vector3 direction) {
    direction=normalize(direction);
    auto const right=normalize(Vector3{direction.z,0.f,-direction.x});
    auto const up=normalize(cross(right,direction));
    ++sequence_;
    if(bursts_.size()>=32)bursts_.erase(bursts_.begin());
    Burst burst{};
    burst.muzzle=muzzle;burst.direction=direction;burst.right=right;
    burst.seed=std::uint32_t(sequence_*2654435761ull);
    // Each shot seeds a bounded set of independent particles. Every velocity,
    // lifetime and angular phase is fixed at birth, not re-randomized per frame.
    burst.particles.reserve(26);
    for(std::uint32_t i=0;i<12;++i) {
        auto const base=burst.seed+i*94103u;
        auto const azimuth=(jitter(base+11u)+1.f)*pi;
        auto const radial=0.75f+0.65f*(jitter(base+29u)+1.f);
        auto const speed=2.4f+2.6f*(jitter(base+47u)+1.f);
        Particle p{};
        p.kind=1;p.seed=base;
        p.origin=muzzle+direction*.012f;
        p.velocity=direction*speed+
            right*(std::cos(azimuth)*radial)+
            up*(std::sin(azimuth)*radial);
        p.start=float(i%5)*.0013f;
        p.life=.085f+.12f*(jitter(base+61u)+1.f)*.5f;
        p.width=.0035f+.0035f*(jitter(base+79u)+1.f)*.5f;
        p.stretch=.032f+.050f*(jitter(base+89u)+1.f)*.5f;
        p.rotation=azimuth;
        burst.particles.push_back(p);
    }
    for(std::uint32_t i=0;i<14;++i) {
        auto const base=burst.seed+i*21157u+0x5abf1e2fu;
        auto const azimuth=(jitter(base+13u)+1.f)*pi;
        Particle p{};
        p.kind=2;p.seed=base;
        p.origin=muzzle+direction*(.006f+.018f*(1.f+jitter(base+31u))*.5f);
        p.velocity=direction*(.18f+.55f*(1.f+jitter(base+37u))*.5f)+
            right*(std::cos(azimuth)*(.035f+.13f*(1.f+jitter(base+41u))))+
            up*(std::sin(azimuth)*.07f)+
            Vector3{0.f,.16f+.16f*(1.f+jitter(base+43u))*.5f,0.f};
        p.start=float(i%7)*.010f;
        p.life=.85f+.55f*(1.f+jitter(base+53u))*.5f;
        p.width=.023f+.012f*(1.f+jitter(base+67u))*.5f;
        p.stretch=.8f+.4f*(1.f+jitter(base+83u))*.5f;
        p.rotation=azimuth;
        burst.particles.push_back(p);
    }
    bursts_.push_back(std::move(burst));
    rebuild_visuals();
}
bool FpsWeaponEffects::emit_shot(Vector3 eye,Vector3 direction) {
    if(!finite(eye)||!finite(direction)||direction.magnitude()<.5f)
        return false;
    direction=normalize(direction);
    auto const right=normalize(Vector3{direction.z,0.f,-direction.x});
    // Legacy camera-origin QA only; scripted weapons call emit_at() with
    // the animated, authored muzzle socket.
    spawn_burst(eye+direction*.65f+right*.11f-Vector3{0.f,.17f,0.f},
                direction);
    return true;
}
bool FpsWeaponEffects::emit_at(Vector3 muzzle,Vector3 direction) {
    if(!finite(muzzle)||!finite(direction)||direction.magnitude()<.5f)
        return false;
    spawn_burst(muzzle,direction);
    return true;
}
bool FpsWeaponEffects::on_shot(PhysicsScene& physics,Vector3 eye,Vector3 direction) {
    if(!finite(eye)||!finite(direction)||
       direction.magnitude()<.5f)return false;
    direction=normalize(direction);
    auto const right=normalize(Vector3{direction.z,0.f,-direction.x});
    auto const up=Vector3{0.f,1.f,0.f};
    auto const muzzle=eye+direction*.65f+right*.11f-up*.17f;
    if(!emit_shot(eye,direction))return false;
    constexpr std::size_t max_persistent_brass=2048;
    if(!physics.supports_runtime_entities()||
       casings_.size()>=max_persistent_brass) {
        ++rejected_casings_;
        return false;
    }
    SceneObject casing;
    casing.name="Pistol casing";
    casing.node.layer="Weapon debris";
    casing.node.id=0xF15A000000000000ull+sequence_;
    casing.node.collision=true;
    casing.position=muzzle-right*.015f+up*.075f;
    casing.size=.042f;
    casing.node.scale={.65f,1.65f,.65f};
    casing.mass=.008f;
    casing.color=0xd5a742u;
    try {
        auto const index=physics.spawn_runtime(casing);
        casings_.push_back(index);
        auto const variation=jitter(std::uint32_t(sequence_));
        physics.apply_impulse(index,
            right*(.014f+variation*.002f)+
            up*(.011f+variation*.001f)-
            direction*.002f);
        return true;
    } catch(std::exception const& e) {
        if(rejected_casings_<3)
            std::fprintf(stderr,"[WARN] [engine.weaponfx] casing: %s\n",e.what());
        ++rejected_casings_;
        return false;
    }
}
void FpsWeaponEffects::advance(float dt) {
    if(!std::isfinite(dt)||dt<0.f||dt>.25f)
        throw std::invalid_argument("Weapon effect delta must be 0..0.25 s");
    for(auto& burst:bursts_)burst.age+=dt;
    bursts_.erase(std::remove_if(bursts_.begin(),bursts_.end(),
        [](Burst const& b){return b.age>=1.50f;}),bursts_.end());
    rebuild_visuals();
}
void FpsWeaponEffects::rebuild_visuals() {
    visuals_.clear();
    visuals_.reserve(bursts_.size()*28);
    for(auto const& burst:bursts_) {
        auto const age=burst.age;
        // Muzzle flash blooms for only 40-65ms, never a long-lived glowing
        // cube. The original socket is the center of the principal disc.
        if(age<.065f) {
            auto const brightness=fadeout(age/.065f,1.8f);
            visuals_.push_back({burst.muzzle+burst.direction*.010f,
                {.14f*brightness,.14f*brightness,.03f},
                0xffe7b9u,0,brightness,{},0.f});
            visuals_.push_back({burst.muzzle+burst.direction*.055f,
                {.085f*brightness,.115f*brightness,.035f},
                0xffc45eu,0,brightness*.72f,{},pi*.25f});
        }
        for(auto const& p:burst.particles) {
            auto const t=age-p.start;
            if(t<0.f || t>=p.life)continue;
            const auto u=t/p.life;
            if(p.kind==1) {
                // Physically interpretable arc: fast forward/radial ejection,
                // velocity drag and Earth gravity. All positions are derived
                // analytically, so subframe hitches cannot change trajectories.
                constexpr float drag=6.f;
                auto const travel=(1.f-std::exp(-drag*t))/drag;
                const auto position=p.origin+p.velocity*travel-
                    Vector3{0.f,4.905f*t*t,0.f};
                const auto motion=p.velocity*std::exp(-drag*t)-
                    Vector3{0.f,9.81f*t,0.f};
                const auto opacity=.95f*fadeout(u,1.4f);
                const auto length=p.stretch*(.55f+.45f*(1.f-u));
                visuals_.push_back({position,{p.width,length,length},
                    0xff9d21u,1,opacity,motion,p.rotation});
            } else {
                // Local expansion, slow warm convection, spatially different
                // vorticity and critically *dissolving* alpha, rather than
                // three equally sized gray spheres moving together.
                const auto drift=(1.f-std::exp(-2.4f*t))/2.4f;
                const auto swirl=.018f*t*(1.f+.3f*jitter(p.seed+101u));
                const auto pos=p.origin+p.velocity*drift+
                    Vector3{0.f,.095f*t+.070f*t*t,0.f}+
                    burst.right*(swirl*std::sin(p.rotation+7.2f*t))+
                    Vector3{0.f,swirl*.55f*std::cos(p.rotation+5.3f*t),0.f};
                const auto expansion=p.width+(.095f+.042f*p.stretch)*
                    std::pow(u,.74f);
                const auto opacity=.30f*smoothstep(t/.095f)*
                    fadeout(u,1.45f);
                if(opacity<.006f)continue;
                visuals_.push_back({pos,{expansion,expansion,expansion},
                    0x9d8b77u,2,opacity,{},p.rotation+.48f*t});
            }
        }
    }
}
}
