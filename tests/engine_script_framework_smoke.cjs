"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const api=path.resolve(__dirname,
    "../engine/northstarcpp/resources/sharedAssets/api");
const listeners=new Map(),warnings=[],mutations=[],voices=[],contactQueue=[];
let drains=0;
let records=[{index:0,id:"101",name:"Door",position:{x:0,y:0,z:1}}];
const engine={
    on(name,handler){
        if(!listeners.has(name))listeners.set(name,new Set());
        listeners.get(name).add(handler);
        return ()=>listeners.get(name).delete(handler);
    },
    off(name,handler){return listeners.get(name)?.delete(handler)??false;},
    emit(name,payload){for(const fn of [...listeners.get(name)??[]])fn(payload);},
    scene:{entities:{
        objects:(start=0,count=256)=>records.slice(start,start+count),
        objectCount:()=>records.length,
    },transforms:{
        moveStatic:(index,...coords)=>mutations.push([index,...coords]),
        rotateStatic:(index,...coords)=>mutations.push(["rotate",index,...coords])
    }},
    physics:{contacts:{pollContacts:()=>{drains++;return contactQueue.splice(0);}}},
    logger:{warn:m=>warnings.push(m),info:()=>{}},
    ecs:{entities:{
        has:(id,key)=>id==="101"&&key==="northstar.position",
        get:(id,key)=>{assert.equal(id,"101");assert.equal(key,"northstar.position");
            return {x:1,y:2,z:3};},
        setPosition:(id,x,y,z)=>mutations.push([id,x,y,z]),
        query:key=>key==="northstar.position"?["101"]:["101","202"]
    }},
    audio:{playback:{
        playNac:(...args)=>{voices.push(["play",...args]);return 42;}
    },voices:{
        supportsVoices:()=>true,
        stopVoice:id=>voices.push(["stop",id]),
        setVoiceGain:(id,gain)=>voices.push(["gain",id,gain])
    },spatial:{
        setVoicePosition:(...args)=>voices.push(["position",...args])
    }}
};
const context=vm.createContext({engine});
vm.runInContext(fs.readFileSync(path.join(api,"northstar/bootstrap.js"),"utf8"),context);
const S=vm.runInContext("NorthStar.modules.engine",context);
vm.runInContext(fs.readFileSync(path.join(api,"scene/Entity.js"),"utf8"),context);
assert.equal(S.API.constructor,S.EngineAPI);
assert.equal(S.API.ecs,S.ECS);
assert.equal(S.API.time,S.Time);
assert.equal(S.API.assets,S.Assets);
const original=S.Scene.handleByName("Door");
assert.equal(original.constructor.name,"AuthoredEntityHandle");
assert.equal(original.alive,true);
assert.equal(original.getComponent("Position").x,1);
assert.equal(original.hasComponent("Position"),true);
assert.equal(S.ECS.query(["Position","Visual"]).join(","),"101");
original.setComponent("Position",{x:2,y:3,z:4});
assert.equal(mutations.at(-1)[0],"101");
const audio=S.Audio.playVoice({asset:"audio/sfx/door.nac",clip:"open",
    position:{x:0,y:1,z:2}});
assert.equal(audio.constructor.name,"AudioVoice");
audio.setGain(.75).setPosition({x:1,y:2,z:3});
assert.equal(audio.stop(),true);
assert.equal(audio.stop(),false);
assert.deepEqual(voices.map(x=>x[0]),["play","gain","position","stop"]);
const contacts=[];
const releaseA=S.Physics.onContacts(batch=>contacts.push(["a",batch]));
const releaseB=S.Physics.onContacts(batch=>contacts.push(["b",batch]));
contactQueue.push({first:0,second:-1,phase:0,response:2});
engine.emit("update",.1);
assert.equal(drains,1,"Native drain must be called once for all subscribers");
assert.equal(contacts.length,2,"Both subscribers must receive the same batch");
assert.equal(contacts[0][1],contacts[1][1]);
assert.ok(Object.isFrozen(contacts[0][1]));
assert.equal(S.Physics.contacts().length,1);
assert.equal(releaseA(),true);
engine.emit("update",.1);
assert.equal(drains,2);
assert.equal(contacts.length,3);
assert.equal(releaseB(),true);
engine.emit("update",.1);
assert.equal(drains,2,"No subscribers means no native polling");
let starts=0, updates=0, eventCount=0, cleanup=0, timerCount=0;
class DoorScript extends S.EntityScript {
    onStart(){
        starts++;
        this.listen("door.use",()=>eventCount++);
        this.own({dispose(){cleanup++;}});
        this.after(.11,()=>timerCount++);
    }
    onUpdate(_dt){updates++;}
}
const script=S.Factory.entityScript(DoorScript,original);
assert.equal(script.installed,true);
assert.equal(script.entity,original);
engine.emit("start");
engine.emit("door.use",{});
engine.emit("update",.06);
engine.emit("update",.06);
assert.equal(timerCount,1);
assert.equal(starts,1);
assert.equal(eventCount,1);
assert.equal(updates,2);
assert.equal(script.context.scheduler.pending,0);
assert.ok(S.Time.frame>=2);
engine.emit("stop");
assert.equal(script.installed,false);
assert.equal(cleanup,1);
engine.emit("door.use",{});
engine.emit("update",.02);
assert.equal(eventCount,1);
assert.equal(updates,2);
assert.equal(script.uninstall(),false);
const oldMutationCount=mutations.length;
records=[{index:0,id:"202",name:"Replacement",position:{x:0,y:0,z:0}}];
assert.equal(original.alive,false);
assert.throws(()=>original.setPosition({x:0,y:0,z:0}),/stale/);
assert.throws(()=>original.getComponent("Position"),/stale/);
assert.equal(mutations.length,oldMutationCount);
class Failing extends S.Script {update(){throw Error("author script failure");}}
const failing=S.Factory.script(Failing);
engine.emit("update",.05);
assert.equal(failing.installed,false);
assert.ok(warnings.some(s=>s.includes("author script failure")));
assert.equal(S.Assets.available("prefab"),true);
assert.equal(S.Assets.available("texture"),false);
assert.throws(()=>S.Assets.open("textures/x.ytd","texture"),/unavailable/);
console.log("PASS script framework: stable ECS entity IDs, managed voices, one physics drain, owned script lifecycle, scheduler and error isolation");
