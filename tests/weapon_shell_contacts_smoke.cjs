"use strict";
// Root SDK regression: a Jolt contact, not firing time, triggers a shell sound.
// NAC tracks are original provided shell_contact_* and shell_roll_* variants.
const fs=require("node:fs");
const vm=require("node:vm");
const path=require("node:path");
const assert=require("node:assert/strict");
const bootstrap=fs.readFileSync(path.join(__dirname,"..",
    "engine/northstarcpp/resources/sharedAssets/api/northstar/bootstrap.js"),"utf8");
const calls=[],contacts=[];
const ctx={engine:{
    scene:{entities:{objects:(start)=>start===77?
        [{index:77,position:{x:2.5,y:.12,z:-1.25}}]:[]}},
    logger:{warn:m=>calls.push(["warn",m])},
    physics:{contacts:{
        pollContacts:()=>contacts.splice(0)},bodies:{
        setBodyMaterial:(index,friction,restitution)=>{
            calls.push(["material",index,friction,restitution]);return true;
        },
        applyImpulse:(index,x,y,z)=>calls.push(["impulse",index,x,y,z])
    }},
    audio:{playback:{playNac:(asset,cue,variation,gain,...position)=>{
        calls.push(["sound",cue,variation,gain,...position]);return 1;
    }}},
    weaponfx:{effects:{emitShot:()=>true}}
}};
vm.runInNewContext(bootstrap,ctx);
ctx.NorthStar.sceneTypes.registerPrefab(class Prefab {
    static instantiate(_path,options) {
        calls.push(["spawn",options.position]);
        return {root:{index:77},alive:true};
    }
});
const fx=ctx.NorthStar.modules.engine.WeaponEffects;
const shot={muzzle:{x:0,y:1,z:0},
    barrelDirection:{x:0,y:0,z:1},
    casing:{origin:{x:.03,y:1,z:0},
        right:{x:1,y:0,z:0},up:{x:0,y:1,z:0}}};
const fired=fx.fire({shot,casingAudio:"audio/weapon/pistol/pistol.nac",
    casingMaterial:{friction:.24,restitution:.53}});
assert.ok(fired.casing);
assert.ok(calls.some(c=>c[0]==="material"&&c[1]===77&&c[2]===.24&&c[3]===.53));
assert.equal(calls.filter(c=>c[0]==="sound").length,0,
    "The casing must be silent until a real physics contact");
const impact=()=>{contacts.push({first:77,second:-1,phase:0,response:2});fx.update(.14);};
impact();
impact();
impact();
let cues=calls.filter(c=>c[0]==="sound").map(c=>c[1]);
assert.deepEqual(cues,["shell_contact_hard",
    "shell_contact_medium","shell_contact_small","shell_roll"]);
assert.ok(calls.filter(c=>c[0]==="sound").every(
    c=>c.length===7&&c[4]===2.5&&c[5]===.12&&c[6]===-1.25),
    "Shell impact audio must follow its actual body position");
assert.equal(fx.stats().contactSounds,3);
assert.equal(fx.stats().rollSounds,1);
fx.update(.14);
assert.equal(calls.filter(c=>c[0]==="sound").length,4,
    "No synthesized contact sounds between physical collisions");
// End and overlap are not impacts.
contacts.push({first:77,second:-1,phase:1,response:2});
contacts.push({first:77,second:-1,phase:0,response:1});
fx.update(.14);
assert.equal(calls.filter(c=>c[0]==="sound").length,4);
console.log("PASS shell: native contacts, NAC impact levels, roll, friction/restitution");
