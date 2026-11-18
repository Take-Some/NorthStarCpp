// Compile-time public SDK contract for all project types.
import {API, EngineAPI, Factory, WeaponProfile, Vector3, Entity, VehicleHandle,
        WeaponController, PhysicsHit} from "@northstar/engine";

const singleton: EngineAPI = API;
const spawnPoint: Vector3 = {x:1,y:2,z:3};
const entity: Entity = Factory.entity({name:"Crate",position:spawnPoint});
entity.setPosition({x:0,y:1,z:0});
const fromGeneric: Entity = Factory.create("entity",{position:spawnPoint});
const existing = Factory.existing("Authored",false);
if(existing?.alive) existing.setPosition(spawnPoint);
const bus = Factory.prefab("prefabs/bus.nprefab",{position:spawnPoint});
const drive: VehicleHandle = Factory.vehicle(bus.root.index);
drive.drive({throttle:0.2,brake:0.1});
drive.navigate({x:10,z:20,speed:12});
const cv = Factory.camera({initialMode:"third_person"});
cv.setMode("first_person");
const sphereHit = API.collision.traceSphere(spawnPoint,{x:3,y:2,z:4});
const hit: PhysicsHit | undefined = sphereHit?.hit;
const audioVoice: number | void = API.audio.play({
    asset:"audio/robots/voice.nac",clip:"greeting",gain:0.75
});
const vfxResult: boolean = API.particles.tryEmit("world.dust",{position:spawnPoint});
const recoil = API.weaponEffects.kick({pitchRadians:0.04,recoverySeconds:0.3});
const profile: WeaponProfile = {
    enabled:true,prefab:"prefabs/weapon.nprefab",
    grip:{bone:"hand"},position:spawnPoint,
    muzzleLocal:spawnPoint,ejectionLocal:spawnPoint,
    magazineSize:12,reserveAmmo:0,roundsPerSecond:5,reloadSeconds:1.2,
    range:80,sweepRadius:0.01,fireMode:"semi"
};
const weapon: WeaponController = Factory.weapon({
    profile:()=>profile,controls:()=>({trigger:false,reload:false}),
    aim:()=>({mode:"barrel"})
});
weapon.fire();
const extension = Factory.register("project.test",(_descriptor,api)=>api.entity);
extension.dispose();
API.camera.lookAt("track",spawnPoint,{x:0,y:0,z:0});
void [singleton,entity,fromGeneric,hit,audioVoice,vfxResult,recoil,weapon];
