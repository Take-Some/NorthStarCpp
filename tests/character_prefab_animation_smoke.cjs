"use strict";
// Generic animated prefab contract: no Pawn, Player or weapon dependency.
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const assert=require("node:assert/strict");
const ROOT=path.join(__dirname,"..");
const PREFAB=fs.readFileSync(path.join(ROOT,
    "sharedAssets/api/scene/Prefab.js"),"utf8");
const spawned=[],played=[],destroyed=[];
let Prefab;
const scene={
    spawn(config) {
        const index=100+spawned.length;
        const entity={index,id:String(index),alive:true,
            destroy(){if(!this.alive)return false;
                this.alive=false;destroyed.push(index);return true;}};
        spawned.push({entity,config});
        return entity;
    },
    playAnimation(index,clip) {
        assert.match(clip,/\.ycd@/);
        played.push({index,clip});
    }
};
function parseNativeText(relative) {
    const s=fs.readFileSync(path.join(ROOT,relative),"utf8");
    assert.match(s,/^NorthStarCpp\.Scene 3/m);
    assert.doesNotMatch(s,/northstar\.gameplay\.pawn|camera_rig|pistol\.ydd/);
    const parts=s.split("\n").map(line=>line.trim());
    const line=parts.find(line=>line.startsWith("entity "));
    const tokens=line.match(/"[^"]*"|\S+/g).map(
        value=>value.replace(/^"|"$/g,""));
    const root={
        id:tokens[1],parent:tokens[2],name:tokens[3],layer:tokens[4],
        asset:tokens[7],position:{x:Number(tokens[8]),y:Number(tokens[9]),
            z:Number(tokens[10])},size:Number(tokens[11]),
        color:parseInt(tokens[12],16),mass:Number(tokens[14]),collision:false
    };
    const animations=parts.filter(line=>line.startsWith("component "))
        .map(line=>{
            const m=line.match(/^component (\d+) "northstar\.animation\.default" 1 "([a-f0-9]+)"$/);
            assert.ok(m,"Native prefab component must be generic animation.default");
            return {entity:m[1],clip:Buffer.from(m[2],"hex").toString("utf8")};
        });
    return {objects:[root],animations};
}
const descriptors={
    "prefabs/abby.nprefab":parseNativeText(
        "sharedAssets/prefabs/abby.nprefab"),
    "prefabs/another.nprefab":{objects:[{
        id:"1",parent:"0",name:"Generic Skeletal NPC",
        asset:"characters/another/models/actor.ydd",layer:"Default",
        position:{x:0,y:0,z:0},size:1,color:0xabcdef,mass:0,collision:false
    }],animations:[{entity:"1",clip:"characters/another/animations/idle.ycd@idle"}]}
};
const context={
    engine:{prefab:{read:p=>descriptors[p]}},
    NorthStar:{
        modules:{engine:{Scene:scene}},
        sceneTypes:{registerPrefab:ctor=>{Prefab=ctor;}}
    }
};
vm.runInNewContext(PREFAB,context,{filename:"Prefab.js"});
assert.equal(typeof Prefab,"function");
const abbey=Prefab.instantiate("prefabs/abby.nprefab",{
    position:{x:2,y:0,z:3}});
assert.equal(abbey.alive,true);
assert.equal(spawned[0].config.asset,"characters/abby/models/abby.ydd");
assert.equal(spawned[0].config.position.x,2);
assert.match(played[0].clip,/mm-explore\.ycd@abby-mm-explore-idle$/);
const second=Prefab.instantiate("prefabs/abby.nprefab",{
    position:{x:7,y:0,z:9}});
assert.notEqual(abbey.root.index,second.root.index);
const other=Prefab.instantiate("prefabs/another.nprefab");
assert.equal(spawned.at(-1).config.name,"Generic Skeletal NPC");
assert.equal(played.at(-1).clip,
    "characters/another/animations/idle.ycd@idle");
// First-person arm filtering is a per-entity render mode, never a prefab.
assert.equal(spawned.filter(x=>x.config.asset.includes("abby.ydd")).length,2);
// Missing YCD rejects the prefab before spawning a partially initialized actor.
descriptors["prefabs/invalid.nprefab"]={
    objects:descriptors["prefabs/another.nprefab"].objects,
    animations:[{entity:"1",clip:"invalid script"}]
};
const before=spawned.length;
assert.throws(()=>Prefab.instantiate("prefabs/invalid.nprefab"),
    /Invalid prefab YCD/);
assert.equal(spawned.length,before);
assert.equal(abbey.destroy(),1);
assert.equal(second.destroy(),1);
assert.equal(other.destroy(),1);
assert.equal(destroyed.length,3);
console.log("PASS: generic independently animated Abby & other character prefabs, no Pawn/weapon, rollback");
