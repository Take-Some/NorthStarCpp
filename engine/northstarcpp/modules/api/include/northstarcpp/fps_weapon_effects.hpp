#pragma once
// NorthStarCpp first-person pistol effects. Short-lived GPU proxies are
// separate from physically persistent brass bodies; gameplay owns the state.
// No dependency on CDPR assets or the renderer provider ABI.
#include <northstarcpp/scene.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace northstarcpp {
class PhysicsScene;
struct WeaponFxVisual {
    Vector3 position{};
    Vector3 scale{1.f,1.f,1.f};
    std::uint32_t rgb=0xffffff;
    // 0=flash, 1=hot spark, 2=smoke. No opaque debug geometry.
    std::uint8_t kind=0;
    float opacity=1.f;
    Vector3 motion{};
    float rotation=0.f;
};
// Active only for the current Play world; casings have NO lifetime or
// distance-based despawn. The runtime's finite physics-body budget still
// applies, and overflow is reported rather than silently removing brass.
class FpsWeaponEffects {
public:
    void advance(float dt);
    // Emit only muzzle/ejection VFX. This must be used by gameplay when
    // the weapon/physics system already owns its cartridge casings.
    // It never creates or deletes a PhysicsScene body.
    bool emit_shot(Vector3 eye,Vector3 aim);
    // Precise world-space muzzle socket for scripted/custom weapons.
    bool emit_at(Vector3 muzzle,Vector3 aim);
    // Compatibility for standalone FPS QA where no separate casing
    // subsystem exists. Do not use from an equipped weapon with shell FX.
    bool on_shot(PhysicsScene& physics,Vector3 eye,Vector3 aim);
    void reset();
    std::size_t casing_count() const noexcept {return casings_.size();}
    std::size_t rejected_casings() const noexcept {return rejected_casings_;}
    std::vector<std::size_t> const& casing_indices() const noexcept {return casings_;}
    std::vector<WeaponFxVisual> const& visuals() const noexcept {return visuals_;}
private:
    struct Particle {
        Vector3 origin{},velocity{};
        float start=0.f,life=1.f;
        float width=.01f,stretch=.01f,rotation=0.f;
        std::uint32_t seed=0;
        std::uint8_t kind=2;
    };
    struct Burst {
        Vector3 muzzle{},direction{},right{};
        float age=0.f;
        std::uint32_t seed=0;
        std::vector<Particle> particles;
    };
    std::uint64_t sequence_=0;
    std::size_t rejected_casings_=0;
    std::vector<std::size_t> casings_;
    std::vector<Burst> bursts_;
    std::vector<WeaponFxVisual> visuals_;
    void spawn_burst(Vector3 muzzle,Vector3 direction);
    void rebuild_visuals();
};
}
