"use strict";
// 3D weapons, skinned arms and gameplay event sequence; no GPU/device required.
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const assert=require("node:assert/strict");
const project=path.join(__dirname,"..","Projects","AbbyPistolDemo");
const source=fs.readFileSync(path.join(project,"scripts","main.js"),"utf8")
    .replace(/^import \{[^\n]+\} from "@northstar\/engine";\s*/m,"");
const listeners={start:[],update:[],stop:[]};
const animations=[],spawned=[],sounds=[],shakes=[],impulses=[],colors=[];
const attachments=[],detached=[],visualModes=[];
let pressed=false,reload=false,mode="first_person",ammoHud="";
let nextIndex=40;
const pawn={index:0,id:"1",name:"Demo Pawn",position:{x:0,y:0.7,z:-8},mass:2};
const target={index:27,id:"28",name:"Target 01",position:{x:0,y:0,z:10},mass:7};
const sceneVisual={index:43,id:"44",alive:true,position:null,rotation:null,
    setPosition(value){this.position=value;return this;},
    setRotation(value){this.rotation=value;return this;}};
const config={
    character:{
        prefab:"prefabs/abby.nprefab",
        resources:{animations:[
        {name:"idle",source:"characters/abby/animations/mm-explore.ycd",
         clip:"abby-mm-explore-idle"},
        {name:"walk",source:"characters/abby/animations/mm-explore.ycd",
         clip:"abby-mm-explore-walk-loop-fw"}
    ]}},
    weapon:{
        enabled:true,prefab:"prefabs/pistol.nprefab",
        grip:{bone:"r_hand_prop_attachment",offset:{x:0,y:0,z:0},
            rotation:{x:0,y:0,z:0}},
        audio:"audio/weapon/pistol/pistol.nac",
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
    Player:{enabled:true,index:0,entityId:"1",paused:false,
        config,eye:()=>({x:0,y:2.35,z:-8})},
    Prefab:{instantiate:(ref,opts)=>{
        assert.ok(opts.position && ref.startsWith("prefabs/"));
        assert.ok(Object.values(config.weapon).includes(ref) ||
            ref===config.character.prefab);
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
    Input:{mouseDown:id=>id===0 && pressed,
        isDown:id=>id===82 && reload},
    Keys:{R:82},
    Physics:{sweepSphere:(from,to,radius,ignore)=>{
        assert.equal(ignore,0);assert.equal(radius,0.055);
        return {index:27,fraction:0.25};
    },applyImpulse:(index,value)=>impulses.push([index,value])},
    UI:{mount:(layer,items)=>{
        assert.equal(layer,"weapon");
        ammoHud=items.find(x=>x.id==="ammo").props.text;
    },unmount:layer=>assert.equal(layer,"weapon")},
    Audio:{playNac:(file,name)=>{
        assert.equal(file,config.weapon.audio);
        sounds.push(name);return 1;
    }},
    Logger:{info(){},warn(m){assert.fail(m)}}
};
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
assert.equal(visualModes[0].mode,"first_person_arms");
assert.equal(visualModes[0].index,body().index);
assert.equal(body().rotation.x,0,"Abby world body remains at Pawn");
assert.ok(gunClip("idle"));
assert.equal(ammoHud,"12 / 72");
assert.ok(sounds.includes("equip"));
pressed=true;emit("update",0.22);
assert.equal(ammoHud,"11 / 72");
assert.equal(impulses.length,1);
assert.deepEqual(colors[0],[27,0x67D6A6]);
assert.ok(gunClip("p-fire"));
assert.ok(sounds.includes("fire_body"));
assert.equal(shakes.length,1);
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
assert.equal(attachments[1].alignment,"camera_forward",
    "TPS weapon muzzle must point along the aim vector");
assert.equal(visualModes.at(-1).mode,"world");
assert.ok(animations.some(x=>x.index===body().index &&
    x.ref.endsWith("@abby-mm-explore-idle")));
mode="first_person";emit("update",0.1);
assert.equal(body().alive,true,"Returning to FPS retains Abby identity");
assert.equal(spawned.length,1,"No FPS-specific Abby instance");
assert.equal(attachments.length,3);
assert.equal(attachments[2].parent,body().index);
assert.equal(attachments[2].alignment,"camera_forward");
assert.equal(visualModes.at(-1).mode,"first_person_arms");
emit("stop");
assert.ok(spawned.every(x=>!x.alive));
assert.equal(sceneVisual.alive,true,"Authored visual must survive script stop");
console.log("PASS: one Abby prefab across cameras, FPS skinned arms, pistol socket, YCD, NAC, HUD, cleanup");
