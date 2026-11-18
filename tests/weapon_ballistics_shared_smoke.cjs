const fs=require("fs"),vm=require("vm"),assert=require("assert");
const source=fs.readFileSync(__dirname+"/../engine/northstarcpp/resources/sharedAssets/api/northstar/bootstrap.js","utf8");
let calls=[],available=true,fraction=.5,rotation=0,ads=true;
const muzzle={x:.05,y:1.54,z:.32};
const context={engine:{
 player:{eye:()=>({x:0,y:1.62,z:0})},
 camera:{angles:()=>({yaw:0,pitch:0}),mode:()=>"first_person"},
 input:{mouseDown:()=>ads},
 scene:{attachmentPoint:(i,x,y,z)=>{
   if(!available)return null;
   const dz=z-.2,dx=x;
   return {x:muzzle.x+Math.cos(rotation)*dx+Math.sin(rotation)*dz,
           y:muzzle.y+y,
           z:muzzle.z-Math.sin(rotation)*dx+Math.cos(rotation)*dz};
 }},
 physics:{sweepSphere:(x,y,z,a,b,c,r,ignore)=>{
  calls.push({origin:{x,y,z},radius:r,ignore});return calls.length===1?
    {fraction,index:4}:{fraction:.5,index:8};
 }}
}};
vm.runInNewContext(source,context);
const solve=context.NorthStar.modules.engine.WeaponBallistics.solve;
const input={weaponIndex:1,muzzleLocal:{x:0,y:0,z:.2},
 eye:{x:0,y:1.62,z:0},aimDirection:{x:0,y:0,z:1},
 range:100,radius:.006,ignoreIndex:2};
const shot=solve(input);
assert(shot && shot.hit.index===8);
assert.deepStrictEqual(calls[1].origin,muzzle);
const d=shot.direction,t=shot.aimPoint;
const dx=t.x-muzzle.x,dy=t.y-muzzle.y,dz=t.z-muzzle.z;
assert((d.x*dx+d.y*dy+d.z*dz)/Math.hypot(dx,dy,dz)>.9999);
calls=[];fraction=.5;
const {eye,aimDirection,...projectMetadata}=input;
const automatic=solve({...projectMetadata,adsRadius:.002});
assert(automatic && Math.abs(automatic.aimPoint.z-50)<1e-4);
assert(Math.abs(calls[1].origin.y-muzzle.y)<1e-6);
assert.strictEqual(calls[1].radius,.002);
assert.strictEqual(calls[1].ignore,2);
// Hip fire still follows the camera reticle; only flash and gun hardware
// follow the actual barrel axis. The projectile starts at the real muzzle.
calls=[];rotation=Math.PI/2;ads=false;
const hip=solve({...input,radius:.055,adsRadius:.006,
                 ejectionLocal:{x:.015,y:.05,z:.07}});
assert.strictEqual(hip.mode,"sight");
assert(hip.direction.z>.999);
assert.strictEqual(hip.effects.flash.origin,hip.muzzle);
assert.strictEqual(hip.effects.smoke.origin,hip.muzzle);
assert(hip.effects.flash.direction.x>.999);
assert(hip.casing && Math.abs(hip.casing.origin.x-muzzle.x)>.01);
assert.strictEqual(calls.length,2);
assert.deepStrictEqual(calls[1].origin,muzzle);
assert.strictEqual(calls[0].origin.x,0);
assert.strictEqual(calls[1].radius,.055); // hip fire does not use ADS radius
// Detached weapon / NPC can still fire precisely down its barrel, independent
// of the player's camera, and never generates a camera-origin shot.
calls=[];rotation=Math.PI/2;ads=false;
const npc=solve({...input,mode:"barrel",ejectionLocal:null});
assert.strictEqual(npc.mode,"barrel");
assert(npc.direction.x>.999);
assert.deepStrictEqual(calls[0].origin,muzzle);
assert.strictEqual(calls.length,1);
calls=[];rotation=0;ads=true;
calls=[];available=false;
assert.strictEqual(solve(input),null);
assert.strictEqual(calls.length,0);
calls=[];available=true;fraction=.001;
assert.strictEqual(solve(input).blocked,true);
console.log("PASS weapon ballistics: muzzle origin, sight convergence, obstruction");
