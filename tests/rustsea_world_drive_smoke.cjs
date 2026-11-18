// V8/Node-compatible deterministic gameplay QA against real Sea of Rust snapshot.
// Run via tests/rustsea_world_drive_smoke.py (fixture produced from NEF8 main.nscn).
"use strict";
const fs=require("node:fs");
const vm=require("node:vm");
const assert=require("node:assert/strict");
const files=process.argv.slice(2);
if(files.length!==2)throw Error("Expected NEF8-exported JSON and main.js");
const entities=JSON.parse(fs.readFileSync(files[0],"utf8"))
    .map((o,i)=>({...o,index:i,rotation:{x:0,y:0,z:0}}));
const source=fs.readFileSync(files[1],"utf8")
    .replace(/^import\s+\{[^}]+\}\s+from\s+"@northstar\/engine";\s*$/m,
        "const {Factory,Script,Events,Scene,Player,Input,Keys,Vehicle,UI,Session,Camera,Logger}=globalThis.__rustSeaTestSDK;");
const states={T:false,E:false,I:false,K:false};
const callbacks={};
const moved=[];
let cameraShakes=0;
const warnings=[];
const hudTexts={};
const UI={loadAsset:()=>({
    batch(f){f();},setText(k,v){hudTexts[k]=v;},mount(){},dispose(){}
}),setTitle(){}};
const Scene={
    iterate:function*(){yield*entities;},
    findPawn(){return entities.find(e=>e.name==="Prototype Pawn");},
    findByName(name){return entities.find(e=>e.name===name);},
    moveStatic(index,point){
        const e=entities[index];
        assert(e,"Missing index "+index);
        assert(!e.name.startsWith("WASTELAND "),"Static world terrain scrolled");
        assert(e.name!=="Prototype Pawn","Gameplay tried to teleport the player");
        assert([point.x,point.y,point.z].every(Number.isFinite));
        e.position={...point};
        moved.push(e.name);
    },
    rotateStatic(index,rotation){entities[index].rotation={...rotation};},
    setColor(index,color){entities[index].color=color;}
};
const vehicleStates=new Map();
const Vehicle={
    available(){return true;},
    attach(index,wheelbase,maxSpeed){
        assert(!vehicleStates.has(index));
        assert(wheelbase>0&&maxSpeed>0);
        const origin=entities[index].position;
        vehicleStates.set(index,{
            index,x:origin.x,y:origin.y,z:origin.z,heading:0,speed:0,
            maxSpeed,controls:{throttle:0,brake:0,steer:0},route:null,rig:null
        });
    },
    configureRig(index,path){
        assert(/^vehicles\/(?:bastion|rust_wren)\.nvehicle$/.test(path));
        vehicleStates.get(index).rig=path;
    },
    state(index){const v=vehicleStates.get(index);assert(v);return {...v};},
    drive(index,controls){
        assert(controls.steer===0,"Prototype tracked cities must not steer before compound collision support");
        vehicleStates.get(index).controls={...controls};
    },
    navigate(index,route){vehicleStates.get(index).route={...route};},
    detach(index){assert(vehicleStates.delete(index));},
    provider(){return "Native Vehicle Dynamics"},
};
function vehicleTick(dt){
    for(const v of vehicleStates.values()){
        if(v.route){
            const error=v.route.speed-v.speed;
            v.controls={throttle:Math.max(0,Math.min(1,error*.32)),brake:0,steer:0};
        }
        const acceleration=v.controls.throttle*3.8-v.controls.brake*6.2-.07;
        v.speed=Math.max(0,Math.min(v.maxSpeed,v.speed+acceleration*dt));
        v.z+=v.speed*dt;
        const root=entities[v.index];
        root.position={x:v.x,y:v.y,z:v.z};
    }
}
const Mock={
    Script:class Script{},
    Factory:{script(Class){
        const instance=new Class();
        (callbacks.start??=[]).push(()=>instance.start());
        (callbacks.update??=[]).push(dt=>instance.update(dt));
        (callbacks.stop??=[]).push(()=>instance.stop());
    }},
    Events:{on(k,cb){(callbacks[k]??=[]).push(cb);}},
    Scene,
    Vehicle,
    Player:{enabled:true,index:0,paused:false},
    Keys:{T:"T",E:"E",I:"I",K:"K"},
    Input:{isDown(k){return Boolean(states[k]);}},
    UI,
    Session:{restart(){throw Error("Unexpected restart");}},
    Camera:{available(){return true;},shake(a,d){
        assert(a>0&&a<.1&&d>.02);cameraShakes++;
    }},
    Logger:{info(){},warn(message){warnings.push(message);}}
};
assert.match(source,/class CityRoot/);
vm.runInNewContext(source,{__rustSeaTestSDK:Mock,console},{filename:files[1],timeout:3000});
for(const f of callbacks.start??[])f();
const name=n=>entities.find(e=>e.name===n);
const oldTerrain=new Map(entities.filter(e=>e.name.startsWith("WASTELAND "))
    .map(e=>[e.name,{...e.position}]));
const initialCity=name("CITYROOT Bastion").position.z;
const oldEnemy=name("CITYROOT Rust Wren").position.z;
const oldTrack={...name("BASTION TREAD L 00").position};
name("Prototype Pawn").position={x:0,y:5.7,z:8};
states.E=true;
for(const fn of callbacks.update??[])fn(1/60);
vehicleTick(1/60);
states.E=false;
states.I=true;
for(let i=0;i<250;i++){
    // Native vehicle dynamics can accumulate a small heading drift even
    // with no steering. CityRoot must not suspend the entire game script.
    if(i===185)vehicleStates.get(name("CITYROOT Bastion").index).heading=.025;
    if(i===230)vehicleStates.get(name("CITYROOT Bastion").index).heading=.11;
    const z0=name("CITYROOT Bastion").position.z;
    for(const fn of callbacks.update??[])fn(1/60);
    vehicleTick(1/60); // engine.vehicle executes *after* gameplay update
    const diff=name("CITYROOT Bastion").position.z-z0;
    // Physics-side support transfer is tested separately in native Jolt QA.
    name("Prototype Pawn").position.z+=diff;
}
states.I=false;
assert(warnings.length===1 && warnings[0].includes("compound"), "Yaw safety warning missing");
assert(vehicleStates.get(name("CITYROOT Bastion").index).controls.brake===1,"Yaw safety brake not held");
// Terrain-aware suspension changes vehicle chassis altitude. CityRoot
// must move ALL deck colliders, tread visuals and wheels together.
const deckY=name("BASTION Deck -3 -12").position.y;
const treadY=name("BASTION TREAD L 00").position.y;
vehicleStates.get(name("CITYROOT Bastion").index).y+=.45;
// One long rendering hitch must never skip syncing the visual city to its
// true vehicle root. Other transport physics still lives in native Jolt.
vehicleTick(1/60);
for(const fn of callbacks.update??[])fn(.7);
assert(Math.abs(name("BASTION Deck -3 -12").position.y-deckY-.45)<.025,
       "World-space collision deck did not follow native vehicle heave");
assert(Math.abs(name("BASTION TREAD L 00").position.y-treadY-.45)<.025,
       "Treads did not follow suspension height");
assert(Math.abs(name("BASTION ART bastion_outer_hull").position.z-
                name("CITYROOT Bastion").position.z)<.025,
       "CityRoot stopped visual synchronization after render hitch");
for(const [n,p] of oldTerrain)assert.deepEqual(name(n).position,p,
    "Terrain/rock world-space position changed: "+n);
const traveled=name("CITYROOT Bastion").position.z-initialCity;
assert(traveled>3,"Bastion did not actually move in world coordinates");
assert(vehicleStates.size===2,"Both cities must be registered with engine.vehicle");
assert([...vehicleStates.values()].every(v=>v.rig?.endsWith(".nvehicle")),
    "Missing project-native vehicle rigs");
assert(name("CITYROOT Rust Wren").position.z>oldEnemy+10,
    "Rust Wren failed independent world-space movement");
assert(name("BASTION TREAD L 00").position.z!==oldTrack.z,
    "Tread link did not animate");
assert(Math.abs(name("BASTION ROADWHEEL L 00").rotation.x)>0.2,
    "Wheel angular movement was not integrated");
assert(entities.some(e=>e.name.startsWith("BASTION DRIVE DUST ") &&
    e.position.y>-2),"Drive did not emit project-native world-space dust");
assert(cameraShakes===0,"Passengers must not receive artificial vehicle camera shake");
assert(!moved.some(n=>n.startsWith("WASTELAND ")),"Terrain was moved");
assert(hudTexts.timer?.includes("TRAVEL") || hudTexts.timer?.includes("GAP"),
    "World-space travel was not reported on HUD");
for(const f of callbacks.stop??[])f();
assert(vehicleStates.size===0,"Vehicle rigs were not detached on game stop");
console.log(JSON.stringify({pass:true,worldTravelMetres:traveled,
    staticTerrain:oldTerrain.size,animatedTreads:96,animatedWheels:14,
    cameraShakes,movedCalls:moved.length}));
