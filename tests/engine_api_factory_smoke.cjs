// Engine-owned API factory contract, provider delegation and lifecycle.
// Run: node tests/engine_api_factory_smoke.cjs
"use strict";
const assert = require("node:assert/strict");
const vm = require("node:vm");
const fs = require("node:fs");
const path = require("node:path");
const root = path.resolve(__dirname, "../engine/northstarcpp/resources/sharedAssets/api");
const calls = {spawn:0, prefab:0, drive:0, waypoint:0, swept:0,
    muzzle:0, shake:0, recoil:0, shots:0};
const entities = [{index:0,id:"1",name:"Authored",position:{x:0,y:0,z:0}}];
let nextId = 2;
let lastShot = null;
const engine = {
    scene:{
        attachmentPoint:(index,x,y,z)=>{
            const p=entities[index]?.position;
            return p?{x:p.x+x,y:p.y+y,z:p.z+z}:null;
        },
        objects:(from=0,count=256)=>entities.slice(from,from+count),
        objectCount:()=>entities.length,
        spawn:(name,x,y,z,size,mass,color,asset,layer,parent)=>{
            calls.spawn++;
            const index=entities.length;
            const record={index,id:String(nextId++),name,
                position:{x,y,z},size,mass,color,asset,layer,parent};
            entities.push(record);
            return {id:record.id,index};
        },
        destroy:id=>{
            const index=entities.findIndex(e=>e && e.id===id);
            if (index<0) throw new Error("unknown id");
            entities[index]=null;
        }
    },
    prefab:{read:()=>{
        calls.prefab++;
        return {objects:[{id:"10",parent:"0",name:"Generated",
            position:{x:0,y:0,z:0},size:1,mass:0,color:0xffffff,
            scale:{x:1,y:1,z:1},asset:"",layer:"Default",collision:false}]};
    }},
    physics:{sweepSphere:(...args)=>{
        calls.swept++;
        return args[0]===9?null:{index:0,fraction:0.5,materialId:1};
    }},
    weaponfx:{emitShot:(...args)=>{
        calls.muzzle++;
        assert.equal(args.length,6);
        return true;
    }},
    vehicle:{
        controls:(...args)=>{calls.drive++;return args;},
        waypoint:(...args)=>{calls.waypoint++;return args;}
    },
    camera:{
        pushShot:(...args)=>{lastShot=args;},
        shake:()=>{calls.shake++;},
        recoil:()=>{calls.recoil++;}
    },
    player:{controlledIndex:()=>-1,controlledEntity:()=> "0"},
    logger:{info:()=>{},warn:()=>{},error:()=>{},debug:()=>{}}
};
const context = vm.createContext({engine});
for(const f of ["northstar/bootstrap.js","scene/Entity.js","scene/Prefab.js"]) {
    vm.runInContext(fs.readFileSync(path.join(root,f),"utf8"),context,{filename:f});
}
const sdk = vm.runInContext("NorthStar.modules.engine",context);
assert.equal(sdk.API.Factory,sdk.Factory);
assert.equal(sdk.API.entity,sdk.Scene);
assert.equal(sdk.API.prefab,sdk.Prefab);
assert.equal(sdk.API.vehicle,sdk.Vehicle);
assert.equal(sdk.API.physics,sdk.Physics);
assert.equal(sdk.API.particles,sdk.Particles);
assert.deepEqual([...sdk.Factory.kinds()],
    ["entity","prefab","existing","vehicle","weapon","camera","character","script","entityScript"]);
assert.throws(()=>sdk.Factory.create("__proto__",{}),/Unknown factory recipe/);
assert.throws(()=>sdk.Factory.create("constructor",{}),/Unknown factory recipe/);
assert.throws(()=>sdk.Factory.create("toString",{}),/Unknown factory recipe/);
assert.throws(()=>sdk.Camera.clearShot("bad source"),/identifier/);
assert.throws(()=>sdk.Camera.applyPreset("../invalid"),/identifier/);
assert.throws(()=>sdk.Factory.entity({name:"Invalid",position:{x:0,y:0,z:0},unexpected:true}),/Unsupported Scene.spawn option/);
const authored = sdk.Factory.existing("Authored");
assert.equal(authored.id,"1");
assert.equal(calls.spawn,0,"acquisition must never spawn");
assert.equal(sdk.Factory.existing("Missing",false),null);
assert.throws(()=>sdk.Factory.existing("Missing"),/not found/);
const entity=sdk.Factory.create("entity",{
    name:"User",position:{x:1,y:2,z:3}});
assert.equal(entity.alive,true);
assert.equal(calls.spawn,1);
assert.equal(entity.position.x,1);
const created=sdk.Factory.create("prefab",{path:"prefabs/demo.nprefab"});
assert.equal(calls.prefab,1);
assert.equal(calls.spawn,2,"one single-entity prefab spawns exactly once");
assert.equal(created.alive,true);
assert.equal(created.root.snapshot.name,"Generated");
assert.equal(created.destroy(),1);
assert.equal(created.root.alive,false);
assert.equal(entity.alive,true);
const cachedIndex = entity.index;
const originalId = entity.id;
entities[cachedIndex] = {index:cachedIndex,id:"999",name:"Reused",position:{x:7,y:0,z:0}};
assert.notEqual(originalId,"999");
assert.equal(entity.alive,false,"recycled slot must invalidate stale entity handles");
assert.equal(entity.destroy(),false,"destroy must not affect a recycled slot");
assert.equal(entities[cachedIndex].id,"999");
assert.equal(sdk.Vehicle.available(),true);
const vehicle=sdk.Factory.vehicle(17);
assert.equal(vehicle.index,17);
vehicle.drive({throttle:0.5,brake:0});
vehicle.navigate({x:10,z:20,speed:4});
assert.equal(calls.drive,1);
assert.equal(calls.waypoint,1);
assert.throws(()=>vehicle.drive({throttle:2}),/throttle/);
assert.throws(()=>sdk.Factory.vehicle(-1),/index/);
const sweep=sdk.API.collision.traceSphere({x:0,y:0,z:0},{x:10,y:0,z:0});
assert.equal(calls.swept,1);
assert.equal(sweep.point.x,5);
assert.equal(sdk.Collision.lineOfSight({x:9,y:0,z:0},{x:10,y:0,z:0}),true);
assert.equal(sdk.Particles.available("weapon.muzzle"),true);
assert.equal(sdk.Particles.emit("weapon.muzzle",{
    position:{x:1,y:2,z:3},direction:{x:0,y:0,z:20}}),true);
assert.equal(calls.muzzle,1);
assert.equal(sdk.Particles.tryEmit("engine.smoke",{position:{x:0,y:0,z:0}}),false);
assert.throws(()=>sdk.Particles.emit("engine.smoke",{position:{x:0,y:0,z:0}}),/unavailable/);
assert.equal(sdk.WeaponEffects.kick({pitchRadians:0.1}).rotation,true);
assert.equal(calls.shake,1);
assert.equal(calls.recoil,1);
sdk.Camera.lookAt("test",{x:0,y:0,z:0},{x:0,y:0,z:10});
assert.equal(lastShot[0],"test");
assert.equal(lastShot.length,10);
assert.equal(sdk.Factory.weapon().equipped,false);
assert.equal(typeof sdk.Factory.camera().start,"function");
// Explicit AI/free weapon: no possessed Pawn, keyboard, mouse or camera needed.
const npcWeapon=sdk.Factory.weapon({
    profile:()=>({
        enabled:true,prefab:"prefabs/test_weapon.nprefab",
        position:{x:3,y:4,z:5},
        // A free weapon does not need a hand socket or a casing port.
        muzzleLocal:{x:0,y:0,z:0},
        magazineSize:3,reserveAmmo:0,roundsPerSecond:5,
        reloadSeconds:1,range:50,sweepRadius:0.01,
        emitCasing:false,fireMode:"semi"
    }),
    controls:()=>({trigger:false,reload:false})
});
const beforeSpawn=calls.spawn;
npcWeapon.start();
assert.equal(calls.spawn,beforeSpawn+1,
    "AI weapon uses Factory.prefab with a single scene spawn");
assert.equal(npcWeapon.equipped,true);
assert.equal(npcWeapon.ammo,3);
const beforeKick=calls.recoil;
const beforeShot=calls.muzzle;
assert.equal(npcWeapon.fire(),true);
assert.equal(npcWeapon.ammo,2);
assert.equal(calls.muzzle,beforeShot+1,"AI weapon uses shared particles");
assert.equal(calls.recoil,beforeKick,"AI shot must not recoil the player camera");
assert.equal(npcWeapon.fire(),false,"rate limit must apply to manual fire");
npcWeapon.update(.25);
assert.equal(npcWeapon.fire(),true);
npcWeapon.stop();
assert.equal(npcWeapon.equipped,false);
const custom=sdk.Factory.register("project.test",(_,api)=>api===sdk.API?"OK":"FAIL");
assert.equal(sdk.Factory.create("project.test"),"OK");
assert.throws(()=>sdk.Factory.register("project.test",()=>0),/already registered/);
assert.equal(custom.dispose(),true);
assert.equal(custom.dispose(),false);
assert.throws(()=>sdk.Factory.create("project.test"),/Unknown factory/);
assert.throws(()=>sdk.Factory.create("physics",{}),/Unknown factory/);
console.log("Engine API factory: PASS (one core API, delegated creation, lifetime, VFX, collisions, vehicles, extensions)");
