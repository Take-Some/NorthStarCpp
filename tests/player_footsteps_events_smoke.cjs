"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),
      path=require("node:path"),vm=require("node:vm");
const root=path.resolve(__dirname,"../engine/northstarcpp/resources/sharedAssets/api");
const handlers=new Map(),plays=[],warnings=[],steps=[];
let contact={grounded:true,surface:"wood"},intent={x:0,z:1},fail=false;
const actor={index:0,id:"1",position:{x:0,y:0,z:0}};
const engine={
 on(n,f){if(!handlers.has(n))handlers.set(n,new Set());handlers.get(n).add(f);
         return ()=>handlers.get(n).delete(f);},
 off(n,f){return handlers.get(n)?.delete(f)??false;},
 emit(n,p){for(const f of [...(handlers.get(n)||[])])f(p);return 1;},
 scene:{objects:(s=0,c=256)=>[actor].slice(s,s+c),objectCount:()=>1},
 player:{controlledIndex:()=>0,controlledEntity:()=>"1",
         motion:()=>intent,ground:()=>contact},
 audio:{playNac(...a){if(fail)throw Error("no audio");plays.push(a);return plays.length;}},
 logger:{warn:m=>warnings.push(m)}
};
const ctx=vm.createContext({engine});
function load(file,imports=false){
 let src=fs.readFileSync(path.join(root,file),"utf8");
 if(imports){src=src.replace(/^\s*import\s*\{([^}]+)\}\s*from\s*["']@northstar\/engine["']\s*;?/gm,
 (_,names)=>names.split(",").map(n=>"const "+n.trim()+" = NorthStar.modules.engine."+n.trim()+";").join("\n"));
 src="(()=>{"+src+"})();";}
 vm.runInContext(src,ctx,{filename:file,timeout:1500});
}
load("northstar/bootstrap.js");load("northstar/events.js");
const Events=vm.runInContext("NorthStar.modules.engine.Events",ctx);
Events.define("qa.once",p=>typeof p==="number");
let once=0;Events.once("qa.once",()=>once++);Events.emit("qa.once",1);Events.emit("qa.once",2);
assert.equal(once,1);assert.throws(()=>Events.emit("qa.once","bad"),/payload/);
assert.equal(Events.stats().emittedByEvent["qa.once"],2);
assert.equal(Events.history(1)[0].name,"qa.once");
const scoped=Events.scope();let scopedCount=0;
scoped.on("qa.once",()=>scopedCount++);Events.emit("qa.once",3);
scoped.dispose();Events.emit("qa.once",4);assert.equal(scopedCount,1);
const Player=vm.runInContext("NorthStar.player",ctx);
Player.configure({enabled:true});
load("audio/footsteps.js",true);load("player/playerFootsteps.js",true);
Events.on("player.footstep",e=>steps.push(e));
engine.emit("start");
const tick=(n=1)=>{for(let i=0;i<n;i++)engine.emit("update",0.1);};
tick();
for(let i=0;i<8;i++){actor.position.z+=0.2;tick();}
assert.equal(plays.length,1);
assert.equal(plays[0][0],"audio/footsteps/wood.nac");
assert.equal(plays[0][1],"walk");assert.equal(steps[0].surface,"wood");
contact={grounded:false,surface:"wood"};
for(let i=0;i<10;i++){actor.position.z+=0.2;tick();}
assert.equal(plays.length,1);
contact={grounded:true,surface:"wood"};intent={x:0,z:0};
for(let i=0;i<10;i++){actor.position.z+=0.2;tick();}
assert.equal(plays.length,1);
intent={x:0,z:1};actor.position.z+=100;tick();assert.equal(plays.length,1);
for(let i=0;i<8;i++){actor.position.z+=0.2;tick();}
assert.equal(plays.length,2);
Player.setPaused(true);
for(let i=0;i<8;i++){actor.position.z+=0.2;tick();}
assert.equal(plays.length,2);
Player.setPaused(false);fail=true;
for(let i=0;i<9;i++){actor.position.z+=0.2;tick();}
assert.equal(warnings.length,1);
for(let i=0;i<10;i++){actor.position.z+=0.2;tick();}
assert.equal(warnings.length,1);
engine.emit("stop");
assert.equal(Events.stats().byEvent["player.footstep"],1);
assert.throws(()=>Events.emit("player.footstep",{actor:-1}),/payload/);
// Validate default player configuration without scripts/main.js.
// Host now discovers authored player entities and loads shared root scripts.
function defaultPlayerEnabled(objects,controlledIndex=-1) {
    const sceneEngine = {
        player: {controlledIndex:()=>controlledIndex},
        scene: {
            objects(start = 0, count = 256) {
                return objects.slice(start, start + count);
            }
        }
    };
    const sandbox = vm.createContext({engine: sceneEngine});
    vm.runInContext(fs.readFileSync(path.join(root,"northstar/bootstrap.js"),"utf8"), sandbox);
    vm.runInContext(
        "NorthStar.inputTypes.registerKeys(Object.freeze(class Keys {" +
        "static V=86; static Space=32; static Escape=27; static F10=121; static E=69;}));",
        sandbox);
    let source=fs.readFileSync(path.join(root,"player/playerConfig.js"),"utf8");
    source=source.replace(
        /^\s*import\s*\{([^}]+)\}\s*from\s*["']@northstar\/engine["']\s*;?/gm,
        (_match,members)=>members.split(",").map(m =>
            "const "+m.trim()+" = NorthStar.modules.engine."+m.trim()+";").join("\n"));
    vm.runInContext("(()=>{"+source+"})();", sandbox);
    return vm.runInContext("NorthStar.player.enabled",sandbox);
}
assert.equal(defaultPlayerEnabled([actor],0),true,
    "authored player enables default gameplay without project main.js");
assert.equal(defaultPlayerEnabled([],-1),false,
    "non-player scenes cannot trigger player-only scripts");
console.log("PASS: Events contracts, subscription lifetime, player footsteps, NAC playback and scriptless default player");
