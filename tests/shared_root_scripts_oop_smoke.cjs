// Class-based root SDK regression. Loads the actual shared scripts in the
// documented native order, not mocks of their classes or lifecycle.
"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const BASE=path.join(__dirname,"..","engine","northstarcpp","resources","sharedAssets","api");
const handlers=new Map();
const held=new Set();
const moves=[],sprints=[],impulses=[],cameraModes=[],titles=[];
let jumped=0,paused=false,followDistance=4;
const objects=[
    {id:"1",index:0,name:"Pawn",mass:2,position:{x:0,y:1.2,z:0}},
    {id:"2",index:1,name:"Crate",mass:2,position:{x:0,y:0,z:2}}
];
const engine={
    on(name,fn) {
        if(!handlers.has(name))handlers.set(name,new Set());
        handlers.get(name).add(fn);
        return ()=>handlers.get(name).delete(fn);
    },
    off(name,fn){return handlers.get(name)?.delete(fn)??false;},
    emit(name,payload){for(const cb of [...(handlers.get(name)||[])])cb(payload);return 1;},
    input:{
        isDown:key=>held.has(key),mouseDown:()=>false,
        pointerDelta:()=>({x:0,y:0}),wheelDelta:()=>0,gamepad:()=>null
    },
    player:{
        controlledIndex:()=>0,controlledEntity:()=>"1",
        setEyeHeight:()=>{},eye:()=>({x:0,y:2.0,z:0}),
        move:(x,z)=>moves.push([x,z]),
        sprint:on=>sprints.push(on),jump:()=>++jumped,
        motion:()=>({x:0,z:0}),
        ground:()=>({grounded:false,surface:"stone"})
    },
    scene:{
        objects:(start=0,count=256)=>objects.slice(start,start+count),
        objectCount:()=>objects.length,
        setColor:()=>{}
    },
    camera:{
        available:()=>true,angles:()=>({yaw:0,pitch:0}),
        mode:()=>cameraModes.at(-1)?.[0]??"first_person",
        setMode:(mode,time)=>cameraModes.push([mode,time]),
        followDistance:()=>followDistance,
        setFollowDistance:d=>{followDistance=d;},
        setAngles:()=>{},setZoom:()=>{}
    },
    physics:{
        sweepSphere:()=>({index:1,fraction:0.5}),
        applyImpulse:(...args)=>impulses.push(args)
    },
    session:{pause:value=>{paused=value;},quit:()=>{},restart:()=>{}},
    ui:{setTitle:title=>titles.push(title),apply:()=>null},
    audio:{playNac:()=>1},
    logger:{warn:()=>{},info:()=>{}}
};
const context=vm.createContext({engine});
function load(relative) {
    const source=fs.readFileSync(path.join(BASE,relative),"utf8").replace(
        /^\s*import\s*\{([^}]+)\}\s*from\s*["']@northstar\/engine["']\s*;?/gm,
        (_unused,imports)=>imports.split(",").map(member=>
            "const "+member.trim()+" = NorthStar.modules.engine."+member.trim()+";").join("\n"));
    vm.runInContext("(()=>{'use strict';\n"+source+"\n})();",context,{
        filename:relative,timeout:2000
    });
}
load("northstar/bootstrap.js");
load("northstar/events.js");
for(const file of [
    "input/Keys.js","input/GamepadButtons.js",
    "scene/Entity.js","scene/Prefab.js","ui/UI.js",
    "character/Character.js","player/playerConfig.js",
    "player/playerActor.js","player/playerSession.js",
    "camera/playerCamera.js","camera/playerLook.js",
    "player/playerMovement.js","audio/footsteps.js",
    "player/playerFootsteps.js","character/playerCharacter.js",
    "player/playerInteraction.js"
])load(file);

const SDK=vm.runInContext("NorthStar.modules.engine",context);
assert.equal(SDK.Player.enabled,true);
assert.equal(SDK.Scene.findPawn().index,0);
assert.equal(SDK.Scene.findPlayer().index,0);
const action=SDK.Input.createAction(SDK.Keys.Space,SDK.GamepadButtons.Jump);
assert.equal(action.pressed(),false);
held.add(SDK.Keys.Space);
assert.equal(action.pressed(),true);
assert.equal(action.pressed(),false);
action.reset();
assert.equal(action.pressed(),true);
held.delete(SDK.Keys.Space);
action.pressed();

class Probe extends SDK.Script {
    started=0;updated=0;stopped=0;
    start(){this.started++;}
    update(){this.updated++;}
    stop(){this.stopped++;}
}
const probe=new Probe();
probe.install();
assert.equal(probe.installed,true);
assert.throws(()=>probe.install(),/already installed/);

engine.emit("start");
assert.equal(probe.started,1);
assert.equal(cameraModes.at(-1)[0],"third_person");
held.add(SDK.Keys.W);
held.add(SDK.Keys.Shift);
engine.emit("update",0.016);
assert.ok(moves.at(-1)[1]>.9);
assert.equal(sprints.at(-1),true);

held.add(SDK.Keys.Space);
engine.emit("update",0.016);
engine.emit("update",0.016);
assert.equal(jumped,1,"jump must be edge-triggered");
held.delete(SDK.Keys.Space);
engine.emit("update",0.016);

held.add(SDK.Keys.E);
engine.emit("update",0.016);
engine.emit("update",0.016);
assert.equal(impulses.length,1,"interaction must be edge-triggered");
assert.equal(impulses[0][0],1,"interaction must ignore the controlled Pawn");
held.delete(SDK.Keys.E);
engine.emit("update",0.016);

held.add(SDK.Keys.V);
engine.emit("update",0.016);
assert.equal(cameraModes.at(-1)[0],"first_person");
engine.emit("update",0.016);
assert.equal(cameraModes.length,2,"camera should not retrigger held V");
held.delete(SDK.Keys.V);

held.add(SDK.Keys.Escape);
engine.emit("update",0.016);
assert.equal(paused,true);
engine.emit("update",0.016);
assert.equal(paused,true);
held.delete(SDK.Keys.Escape);
engine.emit("update",0.016);
held.add(SDK.Keys.Escape);
engine.emit("update",0.016);
assert.equal(paused,false);
held.delete(SDK.Keys.Escape);

engine.emit("stop");
assert.equal(probe.stopped,1);
const before=probe.updated;
assert.equal(probe.uninstall(),true);
assert.equal(probe.uninstall(),false);
engine.emit("update",0.016);
assert.equal(probe.updated,before,"uninstalled script must not receive ticks");
console.log("Shared roots OOP: PASS (config, camera, player, interaction, scoped lifecycle)");
