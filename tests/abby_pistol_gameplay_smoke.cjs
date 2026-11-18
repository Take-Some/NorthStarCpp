// Abby/Pistol project-only gameplay regression (no GPU/audio device required).
"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const root=path.join(__dirname,"..");
const project=path.join(root,"Projects","NorthStarFirstPersonPlatform");
const text=fs.readFileSync(path.join(project,"assets","scripts","main.js"),"utf8")
    .replace(/^import \{[\s\S]*?\} from "@northstar\/engine";\s*/m,"");
const events=new Map();
const hooks={};
for(const name of ["start","update","stop"])events.set(name,[]);
const pawn={index:0,id:"1",name:"Abby Pawn",position:{x:4,y:1.2,z:-12},mass:0};
const objects=[pawn];
const instances=[];
const sounds=[],ui=[],impulses=[],shots=[],warns=[];
let down=false,reload=false,clock=0;
const weaponConfig={
    enabled:true,prefab:"prefabs/weapon/pistol.nprefab",
    audio:"audio/weapon/pistol/pistol.nac",
    magazineSize:12,reserveAmmo:72,roundsPerSecond:5,
    reloadSeconds:1.65,range:110,sweepRadius:.055,
    impactImpulse:14,fireGain:.95
};
class Script {
    own(item){return item;}
    install() {
        for(const name of ["start","update","stop"])
            events.get(name).push(arg=>this[name](arg));
        return this;
    }
}
const weaponPose={position:null,rotation:null,dead:false};
const context={
    Script,
    Events:{on:(n,fn)=>events.get(n).push(fn)},
    Factory:{
        script:Type=>new Type().install(),
        prefab:(file,opts)=>context.Prefab.instantiate(file,opts)
    },
    Scene:{
        findPawn:()=>pawn,
        get:index=>objects[index]||null,
        spawn:params=>{
            const result={index:objects.length,name:params.name,
                mass:params.mass,size:params.size,position:{...params.position}};
            objects.push(result);
            return result;
        },
        setColor(){},
    },
    Player:{
        enabled:true,index:0,entityId:"1",paused:false,
        config:{weapon:weaponConfig},
        eye:()=>({x:pawn.position.x,y:pawn.position.y+1.6,z:pawn.position.z})
    },
    Prefab:{instantiate:(file,opts)=>{
        assert.equal(file,weaponConfig.prefab);
        const rootEntity={
            setPosition(pos){weaponPose.position=pos;return this;},
            setRotation(rot){weaponPose.rotation=rot;return this;}
        };
        const value={alive:true,root:rootEntity,destroy(){
            weaponPose.dead=true;this.alive=false;
        }};
        instances.push(value);
        return value;
    }},
    Camera:{
        angles:()=>({yaw:0,pitch:0}),
        mode:()=>"first_person",
        shake:(a,t)=>{shots.push({a,t});}
    },
    Physics:{
        sweepSphere:(from,to,radius,ignore)=>{
            assert.equal(ignore,0);
            assert.equal(radius,weaponConfig.sweepRadius);
            return {index:objects.findIndex(x=>x.name==="Course Pushable Crate"),fraction:.3};
        },
        applyImpulse:(index,v)=>impulses.push({index,v})
    },
    Input:{
        isDown:key=>(key===82?reload:false),
        mouseDown:button=>button===0&&down
    },
    Keys:{R:82,T:84},
    UI:{
        setTitle(){},
        loadAsset:(asset,{layer})=>{
            assert.ok(["ui/weapon_hud.nui","ui/mission_hud.nui"].includes(asset));
            const values=new Map();
            return {
                batch:fn=>fn(),
                setText(id,text){values.set(id,text);return this;},
                mount(){
                    ui.push({layer,nodes:[...values].map(([id,text])=>({
                        id,props:{text}
                    }))});
                    return this;
                },
                dispose(){ui.push({layer,unmounted:true});}
            };
        },
        mount:(layer,nodes)=>ui.push({layer,nodes}),
        unmount:layer=>ui.push({layer,unmounted:true})
    },
    Audio:{
        play:(request)=>{
            if(typeof request==="string"){sounds.push(request);return 1;}
            assert.equal(request.asset,weaponConfig.audio);
            sounds.push(request.clip);
            return 1;
        },
        supportsVoices:()=>false,
        playWav:src=>{sounds.push(src);return 1;},
        playNac:(archive,name,variation,gain)=>{
            assert.equal(archive,weaponConfig.audio);
            sounds.push(name);
            return 1;
        }
    },
    Session:{restart(){throw Error("Unexpected course restart")}},
    Logger:{info(){},warn:msg=>warns.push(msg)}
};
vm.runInNewContext(text,context,{filename:"main.js"});
const emit=(name,dt)=>events.get(name).forEach(fn=>fn(dt));
const latestAmmo=()=>{
    const panel=ui.filter(x=>x.layer==="weapon"&&x.nodes).at(-1);
    return panel.nodes.find(x=>x.id==="ammo").props.text;
};
emit("start");
assert.equal(instances.length,1);
assert.ok(weaponPose.position);
assert.ok(weaponPose.rotation);
assert.deepEqual(warns,[]);
assert.ok(sounds.includes("equip"));
assert.equal(latestAmmo(),"12 / 72");
down=true;
emit("update",.016);
assert.equal(latestAmmo(),"11 / 72");
assert.equal(impulses.length,1);
assert.ok(sounds.includes("fire_body"));
assert.equal(shots.length,1);
down=false;
emit("update",.016);
reload=true;
emit("update",.016);
assert.ok(sounds.includes("reload"));
reload=false;
for(let i=0;i<10;i++)emit("update",.18);
assert.equal(latestAmmo(),"12 / 71");
assert.ok(weaponPose.position.z>pawn.position.z);
assert.equal(warns.length,0);
emit("stop");
assert.equal(weaponPose.dead,true);
assert.ok(ui.some(x=>x.layer==="weapon"&&x.unmounted));
console.log("PASS: Abby prefab, first-person attachment, hitscan impulse, NAC audio, reload, HUD and cleanup");
