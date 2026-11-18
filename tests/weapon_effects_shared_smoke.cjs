"use strict";
// No game project or Qt loop: ensure generic root-SDK shot effects are atomic
// at the muzzle and survive a failed physical casing provider.
const fs=require("node:fs"),vm=require("node:vm");
const assert=require("node:assert/strict"),path=require("node:path");
const source=fs.readFileSync(path.join(__dirname,
    "../engine/northstarcpp/resources/sharedAssets/api/northstar/bootstrap.js"),"utf8");
const events=[];
const ctx={engine:{
    weaponfx:{effects:{emitShot:(...args)=>{events.push(["flash",...args]);return true;}}},
    physics:{bodies:{setBodyMaterial:()=>true,applyImpulse:(index,x,y,z)=>{
        events.push(["impulse",index,x,y,z]);}}},
    camera:{effects:{recoil:(pitch,yaw,duration)=>{
        events.push(["pitchKick",pitch,yaw,duration]);},
        shake:(amplitude,duration)=>{
        events.push(["recoil",amplitude,duration]);}}},
    logger:{warn:(message)=>{events.push(["warn",message]);}}
}};
vm.runInNewContext(source,ctx);
let throwOnSpawn=false;
ctx.NorthStar.sceneTypes.registerPrefab(class Prefab {
    static instantiate(uri,options) {
        events.push(["spawn",uri,options.position]);
        if(throwOnSpawn)throw Error("synthetic physics spawn failure");
        return {root:{index:12}};
    }
});
const effects=ctx.NorthStar.modules.engine.WeaponEffects;
const shot={
    muzzle:{x:0,y:1.6,z:.238},
    barrelDirection:{x:0,y:0,z:1},
    casing:{
        origin:{x:.025,y:1.65,z:.07},
        right:{x:1,y:0,z:0},
        up:{x:0,y:1,z:0}
    }
};
const result=effects.fire({
    shot,casingType:"pistol",shellImpulse:1,
    recoil:{amplitude:.024,duration:.13}
});
assert(result.particles);
assert(result.casing && result.casing.root.index===12);
assert.deepStrictEqual(events.map(event=>event[0]),
    ["flash","recoil","pitchKick","spawn","impulse"]);
assert.deepStrictEqual(events[0].slice(1),[0,1.6,.238,0,0,1]);
assert.deepStrictEqual(
    JSON.parse(JSON.stringify(events[3][2])),shot.casing.origin);
assert(effects.stats().casingFailures===0);
events.length=0;
throwOnSpawn=true;
const failed=effects.fire({
    shot,casingType:"pistol",shellImpulse:1,
    recoil:{amplitude:.024,duration:.13}
});
assert.strictEqual(failed.casing,null);
assert(failed.casingFailed);
assert.deepStrictEqual(events.map(event=>event[0]),
    ["flash","recoil","pitchKick","spawn","warn"]);
assert(effects.stats().casingFailures===1);
console.log("PASS shared weapon FX: muzzle flash, recoil, shell spawn, failure isolation");
