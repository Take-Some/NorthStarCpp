"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const vm=require("node:vm");
const source=fs.readFileSync(
    "engine/northstarcpp/resources/sharedAssets/api/character/Character.js","utf8");
const actor={position:{x:0,y:0,z:0}};
const poses=[];
let Controller=null;
const environment={
    NorthStar:{character:{registerController(klass){Controller=klass;}}},
    engine:{
        scene:{objects:()=>[actor]},
        character:{load(){},pose(i,heading,label){poses.push(label);},
                   hide(){},clear(){}},
        camera:{available:()=>false}
    }
};
vm.runInNewContext(source,environment,{filename:"Character.js"});
assert.ok(Controller);
const assets=(names)=>({
    model:"characters/test/models/human.ydd",
    skeleton:"characters/test/models/human.ymt",
    material:"characters/test/materials/human.ymat",
    textures:"characters/test/textures/human.ytd",
    animations:names.map(name=>({name,source:"characters/test/animations/gait.ycd",clip:name}))
});
const names=["idle","walk","run","sprint","jump","fall"];
const controller=new Controller({resources:assets(names),states:{
    idle:"idle",walking:"walk",running:"run",sprinting:"sprint",
    jumping:"jump",falling:"fall"
}});
controller.start(0);
function step(speed,count=8){
    for(let i=0;i<count;++i){
        actor.position.z+=speed*.1;controller.update(.1);
    }
}
step(1.48);assert.equal(poses.at(-1),"walk");
step(2.95);assert.equal(poses.at(-1),"run");
step(4.44);assert.equal(poses.at(-1),"sprint");
step(0);assert.equal(poses.at(-1),"idle");
controller.stop();
// Legacy characters with no explicit running/sprinting tracks remain valid.
actor.position.z=0;
const legacy=new Controller({resources:assets(["idle","walk","jump","fall"])});
legacy.start(0);step.call(null,0,1); // ensure controller remains isolated
actor.position.z+=4.44*.1;legacy.update(.1);
assert.equal(poses.at(-1),"walk");
legacy.stop();
console.log("PASS generic CharacterController: idle/walk/run/sprint with walk-only fallback");
