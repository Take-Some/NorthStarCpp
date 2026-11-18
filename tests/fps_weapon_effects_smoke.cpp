#include <northstarcpp/fps_weapon_effects.hpp>
#include <northstarcpp/physics.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace northstarcpp;
void check(bool x,char const* reason){if(!x)throw std::runtime_error(reason);}
int main(){
 try {
    Scene scene;scene.version=3;scene.name="Persistent pistol ejection QA";
    PhysicsScene world(scene);
    FpsWeaponEffects fx;
    check(world.supports_runtime_entities(),"No runtime physics bodies");
    check(fx.emit_shot({0,1.6f,0},{0,0,1}),"Visual shot failed");
    check(std::any_of(fx.visuals().begin(),fx.visuals().end(),
        [](WeaponFxVisual const& v){return v.kind==1;}),
        "Shot did not emit moving spark particles");
    check(fx.casing_count()==0 &&world.scene().objects.empty(),
        "Visual shot created a duplicate casing");
    fx.advance(.12f);
    check(std::any_of(fx.visuals().begin(),fx.visuals().end(),
        [](WeaponFxVisual const& v){return v.kind==2;}),
        "Shot did not emit independent smoke particles");
    check(!fx.visuals().empty(),"Smoke expired with muzzle flash");
    fx.advance(.19f);
    fx.advance(.19f);
    check(!fx.visuals().empty(),"Reference smoke should remain at 500ms");
    // Delayed, buoyant smoke persists beyond 600 ms and fades by 1.5 s.
    fx.advance(.11f);
    check(!fx.visuals().empty(),"Smoke terminated prematurely at 610ms");
    for(int i=0;i<7;++i)fx.advance(.20f);
    check(fx.visuals().empty(),"Smoke exceeded its 1.5s lifetime");
    fx.reset();
    check(!fx.on_shot(world,{0,1.6f,0},{0,0,0}),"Invalid aim accepted");
    auto const baseline=world.scene().objects.size();
    for(unsigned i=0;i<24;++i) {
      check(fx.on_shot(world,{0,1.6f,0},{0,0,1}),"Failed casing spawn");
      check(fx.casing_count()==i+1,"Not 1 casing per shot");
    }
    check(!fx.visuals().empty(),"Missing flash smoke visuals");
    for(int i=0;i<240;++i) {
      world.step(1.f/60.f);
      fx.advance(1.f/60.f);
    }
    check(fx.visuals().empty(),"Flash and smoke did not expire");
    check(fx.casing_count()==24,"Casings disappeared from controller");
    check(world.scene().objects.size()==baseline+24,
        "Physical casings disappeared from scene");
    for(auto const idx:fx.casing_indices()){
      check(world.runtime_alive(idx),"Missing physical casing");
      auto const& c=world.scene().objects[idx];
      check(c.mass>0.f && c.node.layer=="Weapon debris",
          "Casing lacks physics/debris identity");
      check(std::isfinite(c.position.y)&&c.position.y>=-.1f&&c.position.y<2.5f,
          "Casing fell through ground");
    }
    fx.reset();
    check(world.scene().objects.size()==baseline+24,
        "Effect reset removed live physical casings");
    std::cout<<"Weapon FX PASS: 24 brass bodies persist; flash and smoke expire\n";
    return 0;
 }catch(std::exception const& e){std::cerr<<"Weapon FX FAIL: "<<e.what()<<"\n";return 1;}
}
