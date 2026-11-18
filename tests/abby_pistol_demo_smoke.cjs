"use strict";
// 3D weapons, skinned arms and gameplay event sequence; no GPU/device required.
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const assert=require("node:assert/strict");
const project=path.join(__dirname,"..","Projects","AbbyPistolDemo");
const source=fs.readFileSync(path.join(project,"assets","scripts","main.js"),"utf8")
    .replace(/^import \{[^\n]+\} from "@northstar\/engine";\s*/m,"");
const listeners={start:[],update:[],stop:[]};
const animations=[],spawned=[],sounds=[],shakes=[],impulses=[],colors=[],effects=[];
const attachments=[],detached=[],visualModes=[],warnings=[];
let socketReady=true;
let pressed=false,reload=false,aiming=false,mode="first_person",ammoHud="";
let nextIndex=40;
let physicsMotion=null;
const actionListeners=new Map();
const emitAction=(name,payload)=>{
    for(const handler of actionListeners.get(name)||[])handler(payload);
};
const pawn={index:0,id:"1",name:"Demo Pawn",position:{x:0,y:0.7,z:-8},mass:2};
const target={index:27,id:"28",name:"Target 01",position:{x:0,y:0,z:10},mass:7};
const sceneVisual={index:43,id:"44",alive:true,position:null,rotation:null,
    setPosition(value){this.position=value;return this;},
    setRotation(value){this.rotation=value;return this;}};
const config={
    character:{
        prefab:"prefabs/characters/abby.nprefab",
        firstPersonViewmodel:{forward:0.52,drop:0.24,right:0.12,armPitch:0},
        resources:{animations:[
        {name:"idle",source:"characters/abby/animations/mm-explore.ycd",
         clip:"abby-mm-explore-idle"},
        {name:"walk",source:"characters/abby/animations/mm-explore.ycd",
         clip:"abby-mm-explore-walk-loop-fw"},
        {name:"jump",source:"characters/abby/animations/traversal.ycd",
         clip:"abby-ground-jump-run"},
        {name:"fall",source:"characters/abby/animations/fall-medium.ycd",
         clip:"abby-fall-medium"},
        {name:"land_soft",source:"characters/abby/animations/mm-explore.ycd",
         clip:"abby-mm-soft-landing"},
        {name:"land_medium",source:"characters/abby/animations/mm-explore.ycd",
         clip:"abby-mm-med-landing"},
        {name:"land_hard",source:"characters/abby/animations/mm-explore.ycd",
         clip:"abby-mm-hard-landing"}
    ]}},
    weapon:{
        enabled:true,prefab:"prefabs/weapon/pistol.nprefab",
        grip:{bone:"r_hand_prop_attachment",offset:{x:0,y:0,z:0},
            rotation:{x:0,y:0,z:0},
            aimDownSights:{forward:0.52,drop:0.115,right:0.063,armPitch:0}},
        audio:"audio/weapon/pistol/pistol.nac",
        hudAsset:"ui/weapon_hud.nui",
        muzzleLocal:{x:0,y:0.066,z:0.238},
        ejectionLocal:{x:.047,y:.064,z:.035},
        magazineSize:12,reserveAmmo:72,roundsPerSecond:5,
        reloadSeconds:1.8,range:110,sweepRadius:0.055,
        impactImpulse:14,fireGain:0.95,
        animations:{dictionary:"animations/weapon/pistol/military-pistol.ycd",
            idle:"idle",fire:"p-fire",reload:"p-reload",empty:"p-semiauto-empty"}
    }
};
class Script {
    install(){
        for(const name of ["start","update","stop"])
            listeners[name].push(payload=>this[name](payload));
        return this;
    }
}
const context={
    Script,
    API:{render:{sky:{configure:options=>{
        assert.equal(options.enabled,true);
        assert.equal(options.timeOfDay,17.5);
        assert.ok(options.cloudCoverage>0&&options.cloudCoverage<1);
        assert.ok(options.dayLengthSeconds>=1200);
        return options;
    }}}},
    Player:{enabled:true,index:0,entityId:"1",paused:false,
        config,motionState:()=>physicsMotion,eye:()=>({x:0,y:2.35,z:-8})},
    Events:{scope:()=>{
        const owned=[];
        return {
            on(name,fn) {
                if(!actionListeners.has(name))actionListeners.set(name,new Set());
                actionListeners.get(name).add(fn);
                const cancel=()=>actionListeners.get(name)?.delete(fn);
                owned.push(cancel);
                return cancel;
            },
            dispose(){owned.forEach(cancel=>cancel());owned.length=0;}
        };
    }},
    Prefab:{instantiate:(ref,opts)=>{
        assert.ok(opts.position && ref.startsWith("prefabs/"));
        assert.ok(Object.values(config.weapon).includes(ref) ||
            ref===config.character.prefab);
        if(ref===config.weapon.prefab) {
            assert.equal(opts.physics,"visual","Equipped weapon must not simulate as a rigid body");
            assert.notEqual(opts.collision,true);
        }
        const actor={reference:ref,index:nextIndex++,alive:true,
            position:null,rotation:null,root:null,
            destroy(){this.alive=false;}};
        actor.root={
            index:actor.index,
            setPosition(value){actor.position=value;return this;},
            setRotation(value){actor.rotation=value;return this;}
        };
        spawned.push(actor);
        return actor;
    }},
    Scene:{
        findPawn:()=>pawn,get:index=>index===27?target:pawn,
        findByName:name=>name==="SkeletalMesh"?
            {index:43,id:"44",parent:"1",name:"SkeletalMesh"}:null,
        handleByName:name=>name==="SkeletalMesh"?sceneVisual:null,
        setColor:(index,color)=>colors.push([index,color]),
        setVisualMode:(index,mode,settings)=>visualModes.push({index,mode,settings}),
        attachToBone:(child,parent,bone,offset,rotation,alignment)=>{
            assert.equal(child,pistol().index);
            assert.equal(bone,"r_hand_prop_attachment");
            attachments.push({child,parent,bone,offset,rotation,alignment});
        },
        detachFromBone:(index)=>detached.push(index),
        playAnimation:(index,ref)=>{
            assert.ok(spawned.find(x=>x.index===index) || index===sceneVisual.index);
            assert.match(ref,/\.ycd@/);
            animations.push({index,ref});
        }
    },
    Camera:{angles:()=>({yaw:0,pitch:0}),mode:()=>mode,
        shake:(intensity,seconds)=>shakes.push([intensity,seconds])},
    WeaponBallistics:{solve:options=>{
        if(!socketReady)return null;
        assert.equal(options.weaponIndex,pistol().index);
        assert.deepEqual(JSON.parse(JSON.stringify(options.muzzleLocal)),
            config.weapon.muzzleLocal);
        return {muzzle:{x:0,y:1.6,z:.23},
                barrelDirection:{x:0,y:0,z:1},
                direction:{x:0,y:0,z:1},
                hit:{index:27,fraction:.25},
                casing:{origin:{x:.05,y:1.65,z:.04},
                        right:{x:1,y:0,z:0},up:{x:0,y:1,z:0}}};
    }},
    WeaponEffects:{
        update:()=>{},
        fire:options=>{
            effects.push(options);
            shakes.push([options.recoil.pitchRadians,
                         options.recoil.recoverySeconds]);
            return {particles:true,casing:{root:{index:51}}};
        },
        kick:recoil=>{shakes.push([recoil.pitchRadians,
                                  recoil.recoverySeconds]);return true;}
    },
    Input:{mouseDown:id=>id===1?aiming:id===0 && pressed,
        isDown:id=>id===82 && reload},
    Keys:{R:82},
    Physics:{sweepSphere:(from,to,radius,ignore)=>{
        assert.equal(ignore,0);
        assert.equal(radius,aiming?0.006:0.055);
        assert.deepEqual(Object.keys(from).sort(),["x","y","z"]);
        assert.ok(Number.isFinite(to.x) && Number.isFinite(to.z));
        return {index:27,fraction:0.25};
    },applyImpulse:(index,value)=>impulses.push([index,value])},
    UI:{loadAsset:(asset,options)=>{
        assert.equal(asset,"ui/weapon_hud.nui");
        assert.equal(options.layer,"weapon");
        return {batch:(fn)=>fn(),setText:(id,text)=>{
            if(id==="ammo")ammoHud=text;
        },mount(){},unmount(){},dispose(){}};
    },mount:(layer,items)=>{
        assert.equal(layer,"weapon");
        ammoHud=items.find(x=>x.id==="ammo").props.text;
    },unmount:layer=>assert.equal(layer,"weapon")},
    Audio:{
      play:({asset,clip})=>{
        assert.equal(asset,config.weapon.audio);
        sounds.push(clip);return 1;
      },
      playNac:(file,name)=>{
        assert.equal(file,config.weapon.audio);
        sounds.push(name);return 1;
    }},
    Logger:{info(){},warn(m){warnings.push(m)}}
};
context.NorthStar={sceneTypes:{Prefab:context.Prefab},
    modules:{engine:{Keys:context.Keys}}};
const controllerSource=fs.readFileSync(path.join(__dirname,"..",
    "engine","northstarcpp","resources","sharedAssets","api",
    "weapon","weapon.js"),"utf8");
const contractsSource=fs.readFileSync(path.join(__dirname,"..",
    "engine","northstarcpp","resources","sharedAssets","api",
    "core","contracts.js"),"utf8");
vm.runInNewContext(contractsSource+
    "\nconst Factory = {prefab: (path,options) => "+
    "NorthStar.sceneTypes.Prefab.instantiate(path,options), "+
    "script: Type => new Type().install(), " +
    "weapon: options => new WeaponController(options)};\n" +
    "globalThis.Factory=Factory;\n"+
    controllerSource+"\nglobalThis.WeaponController = WeaponController;",context);
vm.runInNewContext(source,context,{filename:"AbbyPistolDemo/scripts/main.js"});
const emit=(name,value)=>listeners[name].forEach(fn=>fn(value));
const pistol=()=>spawned.find(x=>x.reference===config.weapon.prefab);
const body=()=>sceneVisual;
const gunClip=track=>animations.some(x=>x.index===pistol().index &&
        x.ref.endsWith("/military-pistol.ycd@"+track));
emit("start");
assert.equal(spawned.length,1,"Authored visual must be reused; only weapon spawns");
assert.ok(pistol().alive && body().alive);
assert.equal(body().position,null,
    "Native Pawn attachment already updates authored SkeletalMesh position");
assert.equal(pistol().position,null,"Gun follows wrist, not camera teleport");
assert.equal(attachments.length,1);
assert.equal(attachments[0].parent,body().index);
assert.equal(attachments[0].alignment,"camera_forward");
assert.equal(visualModes[0].mode,"first_person_body");
assert.equal(visualModes[0].index,body().index);
assert.equal(visualModes[0].settings.right,0.12);
aiming=true;emit("update",0.08);
assert.equal(visualModes.at(-1).mode,"first_person_body");
assert.equal(visualModes.at(-1).settings.right,0.063);
assert.equal(visualModes.at(-1).settings.drop,0.115);
assert.equal(visualModes.at(-1).settings.ads,true);
assert.equal(spawned.length,1,"ADS must not clone or replace Abby");
aiming=false;emit("update",0.08);
assert.equal(visualModes.at(-1).mode,"first_person_body");
assert.equal(visualModes.at(-1).settings.drop,0.24);
assert.notEqual(visualModes.at(-1).settings.ads,true);
assert.equal(body().rotation.x,0,"Abby world body remains at Pawn");
assert.ok(gunClip("idle"));
assert.equal(ammoHud,"12 / 72");
assert.ok(sounds.includes("equip"));
aiming=true;
pressed=true;emit("update",0.22);
assert.equal(ammoHud,"11 / 72");
assert.equal(impulses.length,1);
assert.deepEqual(colors[0],[27,0x67D6A6]);
assert.ok(gunClip("p-fire"));
assert.ok(sounds.includes("fire_body"));
assert.equal(shakes.length,1);
assert.equal(effects.length,1);
assert.equal(effects[0].casingType,"pistol");
aiming=false;
pressed=false;emit("update",0.22);
assert.equal(animations.at(-1).ref.endsWith("@idle"),true);
reload=true;emit("update",0.12);reload=false;
assert.ok(gunClip("p-reload"));assert.ok(sounds.includes("reload"));
for(let i=0;i<14;i++)emit("update",0.14);
assert.equal(ammoHud,"12 / 71");
assert.equal(animations.at(-1).ref.endsWith("@idle"),true);
mode="third_person";emit("update",0.1);
assert.equal(body().alive,true,"Camera toggle must not destroy Abby");
assert.equal(spawned.length,1,"No character spawn on 3P camera");
assert.equal(attachments.length,2);
assert.equal(attachments[1].parent,body().index);
assert.equal(attachments[1].alignment,"socket",
    "TPS pistol must follow the animated hand socket");
assert.equal(visualModes.at(-1).mode,"world");
assert.ok(animations.some(x=>x.index===body().index &&
    x.ref.endsWith("@abby-mm-explore-idle")));
mode="first_person";emit("update",0.1);
assert.equal(body().alive,true,"Returning to FPS retains Abby identity");
assert.equal(spawned.length,1,"No FPS-specific Abby instance");
assert.equal(attachments.length,3);
assert.equal(attachments[2].parent,body().index);
assert.equal(attachments[2].alignment,"camera_forward");
assert.equal(visualModes.at(-1).mode,"first_person_body");
// A temporarily unavailable socket must never suppress mechanical
// YCD fire animation or recoil, even though muzzle VFX cannot be placed.
socketReady=false;
pressed=true;
emit("update",0.24);
assert.equal(ammoHud,"11 / 71");
assert.ok(animations.at(-1).ref.endsWith("@p-fire"));
assert.equal(effects.length,1,"No fabricated camera-origin muzzle FX");
assert.equal(shakes.length,2,"Recoil survives a missing socket");
assert.ok(warnings.some(x=>x.includes("Missing weapon socket snapshot")));
// Native physics events must override horizontal gait and choose one-shot
// landing responses from real fall impact. Weapon and body stay one entity.
pressed=false;
physicsMotion={pawn:"1",state:"jumping",grounded:false,
    horizontalSpeed:0,verticalSpeed:4,fallDistance:0};
emit("update",.10);
assert.ok(animations.some(a=>a.index===body().index &&
    a.ref.endsWith("@abby-ground-jump-run")));
physicsMotion={...physicsMotion,state:"falling",verticalSpeed:-3,fallDistance:1.2};
emit("update",.10);
assert.ok(animations.some(a=>a.index===body().index &&
    a.ref.endsWith("@abby-fall-medium")));
physicsMotion={...physicsMotion,state:"idle",grounded:true,verticalSpeed:0};
for(const [impact,distance,expected] of [
    [2,.4,"abby-mm-soft-landing"],
    [5,1.4,"abby-mm-med-landing"],
    [8,3.4,"abby-mm-hard-landing"]
]) {
    emitAction("player.land",{impactSpeed:impact,fallDistance:distance});
    emit("update",.10);
    assert.ok(animations.some(a=>a.index===body().index &&
        a.ref.endsWith("@"+expected)), "Missing "+expected);
    for(let i=0;i<7;i++)emit("update",.10);
}
emit("stop");
assert.equal(actionListeners.get("player.land")?.size,0,
    "Landing event subscription must be removed at script stop");
assert.ok(spawned.every(x=>!x.alive));
assert.equal(sceneVisual.alive,true,"Authored visual must survive script stop");
console.log("PASS: one authored Abby across cameras, true FPS body, TPS pistol socket, YCD, NAC, HUD, cleanup");
