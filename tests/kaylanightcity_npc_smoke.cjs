"use strict";
// Project NPC logic contract smoke. This mocks engine boundaries; it does not
// replace native Jolt/GPU acceptance. Verifies state, movement, avoidance,
// interaction, lifecycle and that the real project script wires the director.
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const assert=require("node:assert/strict");
const file=path.resolve(__dirname,"../Projects/KaylaNightCity/assets/scripts/main.js");
const code=fs.readFileSync(file,"utf8");
for(const required of ["#npcs.start()","#npcs.update(dt)","#npcs.stop()",
    'Collision } from "@northstar/engine"']) {
    assert(code.includes(required),"missing project integration: "+required);
}
const from=code.indexOf("class NightCityNpcDirector {");
const to=code.indexOf("class ProjectFrontEnd",from);
assert(from>=0&&to>from,"missing standalone NPC behavior class");
const messages=[];
const events=[];
const animations=[];
const positions=new Map([
    ["NPC Ellie",{x:838.5,y:33.88,z:2285}],
    ["NPC Isaac",{x:843.1,y:33.88,z:2284}]
]);
const actors=new Map([...positions].map(([name,pos],i)=>{
    const actor={name,id:String(155+i),index:154+i,alive:true,
        _position:{...pos},moves:0,rotations:0,
        get position(){return {...this._position};},
        setPosition(p){this._position={...p};this.moves++;return this;},
        setRotation(p){this.rotations++;return this;}
    };
    return [name,actor];
}));
let sweepCount=0,blockMovement=false,use=false,player={id:"1",
    position:{x:812,y:33.88,z:2245}};
let mounted=0,disposed=0,labels={};
const ctx={
    Scene:{
        handleByName:name=>actors.get(name)??null,
        playAnimation:(index,clip,rate)=>animations.push({index,clip,rate}),
        findPawn:()=>player,
        get:index=>null
    },
    Collision:{
        traceSphere:()=>null,
        sweepSphere:()=>{sweepCount++;return blockMovement?
            {index:1234,fraction:0.15}:null;}
    },
    UI:{loadAsset:()=>({mount(){mounted++},dispose(){disposed++},
        setText(key,value){labels[key]=value;}})},
    Input:{isDown:key=>key==="E"&&use},
    Keys:{E:"E"},
    Events:{emit:(name,data)=>events.push({name,data})},
    Logger:{info:x=>messages.push(x),warn:x=>messages.push(x)}
};
vm.runInNewContext(code.slice(from,to)+"\nglobalThis.Director=NightCityNpcDirector;",ctx);
const npc=new ctx.Director();
npc.start();
assert.equal(events.filter(e=>e.name==="npc.spawn").length,2);
assert.deepEqual([...actors.keys()],["NPC Ellie","NPC Isaac"]);
for(let i=0;i<60;i++)npc.update(.05);
assert([...actors.values()].some(a=>a.moves>0),"NPCs never patrolled");
assert(sweepCount>0,"NPCs did not check real collision service");
assert(events.some(e=>e.name==="npc.state"&&e.data.state==="walk"),
    "NPCs never entered walk state");
assert(animations.some(a=>a.clip.endsWith("@npc-walk")),
    "NPC never selected the authored retargeted YCD walk cycle");
const baselineMoves=[...actors.values()].reduce((n,a)=>n+a.moves,0);
blockMovement=true;
for(let i=0;i<50;i++)npc.update(.05);
assert(events.some(e=>e.name==="npc.state"&&e.data.state==="avoid"),
    "NPCs failed to switch into obstacle avoidance");
assert(animations.some(a=>a.clip.endsWith("@npc-idle")),
    "NPC did not return to the native YCD idle cycle");
assert.equal([...actors.values()].reduce((n,a)=>n+a.moves,0),baselineMoves,
    "NPCs passed through impassable collision");
blockMovement=false;
player={id:"1",position:{...actors.get("NPC Ellie").position}};
use=true;
npc.update(.05);
const talks=events.filter(e=>e.name==="npc.interact");
assert.equal(talks.length,1,"nearby use key did not produce single interaction");
assert.equal(talks[0].data.name,"NPC Ellie");
assert(mounted>0&&labels.npcMessage,"NPC dialogue HUD was not shown");
for(let i=0;i<12;i++)npc.update(.05);
assert.equal(events.filter(e=>e.name==="npc.interact").length,1,
    "held interaction key triggered repeatedly");
npc.stop();
assert(disposed>0,"stop leaked interaction HUD");
console.log("PASS KaylaNightCity NPC patrol / Jolt sweep contract / avoid / dialogue / lifecycle");
