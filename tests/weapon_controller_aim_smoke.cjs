// Regression: the possessed Pawn capsule is ignored and camera recoil
// scales down in ADS, even when the weapon is on a separate visual entity.
"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const script=fs.readFileSync(path.join(__dirname,
    "../engine/northstarcpp/resources/sharedAssets/api/weapon/weapon.js"),"utf8");
const shotRequests=[],cameraKicks=[];
let aiming=false;
const holder={alive:true,root:{index:9},snapshot:{position:{x:0,y:1,z:0}}};
const pistol={alive:true,root:{index:22},destroy(){this.alive=false;}};
const profile={
    enabled:true,prefab:"prefabs/weapon/pistol.nprefab",
    grip:{bone:"r_hand_prop_attachment"},
    muzzleLocal:{x:0,y:.0738,z:.238},emitCasing:false,
    magazineSize:12,reserveAmmo:36,roundsPerSecond:5,
    reloadSeconds:1.8,range:110,sweepRadius:.055,
    recoil:{pitchRadians:.055,yawRadians:.003,
        recoverySeconds:.28,amplitude:.012,duration:.11,
        aimDownSights:{pitchRadians:.024,yawRadians:.001,
            recoverySeconds:.18,amplitude:.004,duration:.08}}
};
const context={
    Script:class {},
    Player:{config:{weapon:profile},index:7,entityId:"pawn-7",
        paused:false,eye:()=>({x:0,y:1.62,z:0})},
    Camera:{mode:()=>"first_person"},
    Input:{mouseDown:(button)=>button===1&&aiming,isDown:()=>false},
    NorthStar:{modules:{engine:{Keys:{R:82}}}},
    Factory:{prefab:(_path,_options)=>pistol},
    Scene:{attachToBone:()=>{},detachFromBone:()=>{},
        playAnimation:()=>{},get:()=>null},
    Logger:{info:()=>{},warn:()=>{}},
    Contracts:{vector:()=>{}},
    WeaponBallistics:{solve:(options)=>{
        shotRequests.push(options);return null;
    }},
    WeaponEffects:{kick:(options)=>{cameraKicks.push(options);}},
};
vm.runInNewContext(script+"\nthis.WeaponController=WeaponController;",context);
const gun=new context.WeaponController({holder:()=>holder});
gun.start();
assert.equal(gun.weaponIndex,22);
gun.fire();
assert.equal(shotRequests[0].ignoreIndex,7); // Pawn, not visual 9
assert.equal(shotRequests[0].weaponIndex,22);
assert.equal(shotRequests[0].mode,"auto");
assert.equal(cameraKicks[0].pitchRadians,.055);
gun.update(.21);
aiming=true;
gun.fire();
assert.equal(shotRequests[1].ignoreIndex,7);
assert.equal(cameraKicks[1].pitchRadians,.024);
assert.equal(cameraKicks[1].amplitude,.004);
assert.equal(cameraKicks[1].recoverySeconds,.18);
assert.equal(gun.ammo,10);
gun.stop();
assert.equal(pistol.alive,false);
console.log("PASS weapon controller: Pawn capsule ignored, muzzle shot, ADS recoil");
