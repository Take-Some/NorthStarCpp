"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const bootstrap=fs.readFileSync(
    path.join(__dirname,"..","engine","northstarcpp","resources",
        "sharedAssets","api","northstar","bootstrap.js"),"utf8");
const subscribers=new Map();
let pending=null;
const player={
    controlledIndex:()=>0,controlledEntity:()=>"17",
    animationState:()=>null,
    motion:()=>({x:0,z:0}),
    motionState:()=>pending,
    ground:()=>({grounded:true,surface:"stone"}),
    jump:()=>null,
    eye:()=>({x:0,y:2,z:0}),
    setEyeHeight:()=>null
};
const engine={
    player:{controller:{controlledIndex:player.controlledIndex,
        controlledEntity:player.controlledEntity},
        motion:{animationState:player.animationState,motion:player.motion,
            motionState:player.motionState,ground:player.ground,
            jump:player.jump,eye:player.eye,setEyeHeight:player.setEyeHeight}},
    on(name,callback){
        if(!subscribers.has(name))subscribers.set(name,new Set());
        subscribers.get(name).add(callback);
        return ()=>subscribers.get(name).delete(callback);
    },
    emit(name,payload) {
        for(const fn of subscribers.get(name)||[])fn(payload);
    }
};
const context={engine};
vm.runInNewContext(bootstrap,context,{filename:"northstar.js"});
const {Player,Events}=context.NorthStar.modules.engine;
assert.equal(Player.motionState(),null);
pending={pawn:"17",state:"jumping",horizontalSpeed:2,
         verticalSpeed:4,fallDistance:0,impactSpeed:0,
         grounded:false,moving:true,landed:false};
assert.equal(Player.motionState().state,"jumping");
const heard=[];
const unsubscribe=Player.on("land",event=>heard.push(event.impactSpeed));
const scoped=Events.scope();
scoped.on("player.fall",event=>heard.push(event.fallDistance));
let count=0;
Player.once("jump",()=>++count);
engine.emit("player.jump",pending);
engine.emit("player.jump",pending);
engine.emit("player.fall",{fallDistance:1.7});
engine.emit("player.land",{impactSpeed:5.4});
assert.equal(count,1);
assert.deepEqual(heard,[1.7,5.4]);
assert.equal(unsubscribe(),true);
scoped.dispose();
engine.emit("player.fall",{fallDistance:99});
engine.emit("player.land",{impactSpeed:99});
assert.deepEqual(heard,[1.7,5.4]);
assert.throws(()=>Player.on("../escape",()=>{}),{name:"TypeError"});
console.log("PASS player action SDK: motionState, on, once, scope cleanup");
