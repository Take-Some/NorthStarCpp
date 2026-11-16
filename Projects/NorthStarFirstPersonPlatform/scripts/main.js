// NorthStar First Person Protocol — component-authored Pawn, Jolt movement,
// project-owned objectives and a provider-neutral GPU-composited mission HUD.
import { Events, Scene, Player, Input, Keys, UI, Session, Logger, Audio, Prefab, Camera, Physics, Script } from "@northstar/engine";

// Game-owned sidearm: camera/physics/audio/scene/UI are replaceable engine APIs.
// The Pawn is possessed by PlayerController; no legacy player flag is required.
class PistolWeapon extends Script {
    #instance=null;
    #config=null;
    #ammo=0;
    #reserve=0;
    #cooldown=0;
    #reload=0;
    #lastReload=false;
    #lastTrigger=false;
    #hudClock=0;
    #shotSerial=0;
    #status="READY";

    #cue(name, variation=0, gain=1) {
        try {
            return Audio.playNac(this.#config.audio,name,variation,gain);
        } catch(error) {
            Logger.warn("[Pistol] Optional NAC cue "+name+": "+String(error));
            return 0;
        }
    }

    #hud() {
        const ammoLabel=this.#reload>0?"RELOADING":this.#status;
        UI.mount("weapon",[
            {id:"panel",type:"panel",props:{
                x:700,y:12,width:320,height:118,padding:10,gap:4,
                background:"#111b2cdd"}},
            {id:"header",type:"text",parent:"panel",props:{
                text:"ABBY  /  PISTOL",height:24,font_scale:2,color:"#f6c753ff"}},
            {id:"ammo",type:"text",parent:"panel",props:{
                text:String(this.#ammo).padStart(2,"0")+" / "+
                    String(this.#reserve).padStart(2,"0"),
                height:35,font_scale:3,color:"#e4f0ffff"}},
            {id:"state",type:"text",parent:"panel",props:{
                text:ammoLabel+"   LMB FIRE  /  RMB AIM  /  R RELOAD",
                height:27,font_scale:1,color:"#87cadfff"}}
        ]);
    }

    #follow() {
        if(!this.#instance?.alive || Player.index<0)return;
        const eye=Player.eye();
        const angle=Camera.angles();
        const yaw=angle.yaw,pitch=angle.pitch;
        const forward={x:Math.sin(yaw)*Math.cos(pitch),
            y:Math.sin(pitch),z:Math.cos(yaw)*Math.cos(pitch)};
        const right={x:Math.cos(yaw),z:-Math.sin(yaw)};
        const third=Camera.mode()==="third_person";
        const ahead=third?0.35:0.45;
        const sideways=third?0.32:0.27;
        const down=third?-0.37:-0.19;
        this.#instance.root.setPosition({
            x:eye.x+forward.x*ahead+right.x*sideways,
            y:eye.y+forward.y*ahead+down,
            z:eye.z+forward.z*ahead+right.z*sideways
        });
        // Visual-only runtime model: no contact collider, no physics fighting
        // the Camera Rig. Rotation remains authored through Scene API.
        this.#instance.root.setRotation({
            x:-pitch*180/Math.PI,y:yaw*180/Math.PI,z:0
        });
    }

    #fire() {
        if(this.#ammo<=0) {
            this.#status="EMPTY";
            this.#cooldown=0.24;
            this.#cue("empty");
            return;
        }
        --this.#ammo;
        this.#cooldown=1/this.#config.roundsPerSecond;
        this.#shotSerial++;
        this.#status="FIRED";
        const eye=Player.eye();
        const {yaw,pitch}=Camera.angles();
        const dx=Math.sin(yaw)*Math.cos(pitch);
        const dy=Math.sin(pitch);
        const dz=Math.cos(yaw)*Math.cos(pitch);
        const start={x:eye.x+dx*.30,y:eye.y+dy*.30,z:eye.z+dz*.30};
        const range=this.#config.range;
        const end={x:start.x+dx*range,y:start.y+dy*range,
            z:start.z+dz*range};
        const hit=Physics.sweepSphere(start,end,this.#config.sweepRadius,
                                       Player.index);
        if(hit && hit.index!==Player.index) {
            this.#status="HIT";
            if(hit.index>=0) {
                const target=Scene.get(hit.index);
                if(target && Number(target.mass)>0) {
                    const impulse=this.#config.impactImpulse;
                    Physics.applyImpulse(hit.index,{
                        x:dx*impulse,y:dy*impulse,z:dz*impulse
                    });
                }
            }
        }
        const primary=this.#cue("fire_body",0,this.#config.fireGain);
        // ABI 1 only supports a single voice; do not cut the attack short.
        if(primary>0) {
            this.#cue("fire_tail",this.#shotSerial%3,0.45);
            this.#cue("fire_reflection",this.#shotSerial%3,0.2);
        }
        Camera.shake(0.026,0.08);
    }

    #beginReload() {
        if(this.#reload>0 || this.#ammo>=this.#config.magazineSize ||
           this.#reserve<=0)return;
        this.#reload=this.#config.reloadSeconds;
        this.#status="RELOADING";
        this.#cue("reload");
    }

    start() {
        if(!Player.enabled || !Player.config.weapon?.enabled)return;
        if(Player.index<0 || !Player.entityId)
            throw new Error("Pistol requires a possessed Abby Pawn");
        this.#config=Player.config.weapon;
        for(const key of ["magazineSize","reserveAmmo","roundsPerSecond",
                          "reloadSeconds","range","sweepRadius","impactImpulse"]) {
            const number=this.#config[key];
            if(!Number.isFinite(number) || number<=0)
                throw new RangeError("Invalid pistol configuration: "+key);
        }
        this.#ammo=this.#config.magazineSize;
        this.#reserve=this.#config.reserveAmmo;
        this.#cooldown=0;this.#reload=0;this.#hudClock=0;
        this.#lastTrigger=false;this.#lastReload=false;
        this.#instance=Prefab.instantiate(this.#config.prefab,{
            position:Player.eye()
        });
        this.#follow();
        this.#cue("equip");
        this.#status="READY";
        this.#hud();
        Logger.info("[Pistol] Abby equipped. Magazine="+this.#ammo+
                    ", reserve="+this.#reserve);
    }

    update(dt) {
        if(!this.#config || !this.#instance?.alive)return;
        if(Player.paused)return;
        if(!Number.isFinite(dt) || dt<0 || dt>0.5)return;
        this.#follow();
        this.#cooldown=Math.max(0,this.#cooldown-dt);
        const reloading=this.#reload>0;
        this.#reload=Math.max(0,this.#reload-dt);
        if(reloading && this.#reload===0) {
            const loaded=Math.min(this.#config.magazineSize-this.#ammo,
                                  this.#reserve);
            this.#ammo+=loaded;this.#reserve-=loaded;
            this.#status="READY";
        }
        const reloadDown=Input.isDown(Keys.R);
        if(reloadDown && !this.#lastReload)this.#beginReload();
        this.#lastReload=reloadDown;
        const trigger=Input.mouseDown(0);
        if(trigger && !this.#lastTrigger && this.#ammo===0 &&
           this.#reserve>0)this.#beginReload();
        if(trigger && this.#reload===0 && this.#cooldown<=0)
            this.#fire();
        this.#lastTrigger=trigger;
        this.#hudClock+=dt;
        if(this.#hudClock>0.12) {
            this.#hudClock=0;
            this.#hud();
        }
    }

    stop() {
        if(this.#instance) {
            this.#instance.destroy();
            this.#instance=null;
        }
        UI.unmount("weapon");
        this.#config=null;
        this.#lastTrigger=false;this.#lastReload=false;
    }
}

const OBJECTIVES = Object.freeze([
    Object.freeze({name:"SIGNAL", instruction:"Reach the cyan navigation beacon"}),
    Object.freeze({name:"PHYSICS", instruction:"Push the yellow crate at least 0.9 m"}),
    Object.freeze({name:"TRANSIT", instruction:"Reach the orange navigation beacon"}),
    Object.freeze({name:"EXTRACTION", instruction:"Reach the green extraction beacon"})
]);

class TrainingCourse {
    #phase=0;
    #elapsed=0;
    #hudClock=0;
    #started=false;
    #completed=false;
    #lastRestart=false;
    #crateInitial=null;
    #crate=null;
    #beacons=[];
    #scenery=[];

    #spawnStatic(name,x,y,z,size,color) {
        const item=Scene.spawn({name,position:{x,y,z},size,mass:0,color});
        this.#scenery.push(item.index);
        return item;
    }

    #spawnBeacon(name,x,z,color) {
        const base=Scene.spawn({
            name:`Course ${name} base`,
            position:{x,y:0.18,z},size:0.30,mass:0,color
        });
        const light=Scene.spawn({
            name:`Course ${name} signal`,
            position:{x,y:1.55,z},size:0.63,mass:0,color
        });
        return {x,z,base:base.index,light:light.index};
    }

    #dressCourse(at) {
        const accent=0x314B68;
        const rail=0xE8B34B;
        // Low industrial navigation pillars frame each checkpoint without
        // enclosing the playable route or introducing a new physics system.
        for(const point of this.#beacons) {
            this.#spawnStatic("Course marker left",point.x-2.3,0.60,point.z,0.55,accent);
            this.#spawnStatic("Course marker right",point.x+2.3,0.60,point.z,0.55,accent);
        }
        for(const [i,x,z] of [
            [0,at.x+4,at.z+0.5],
            [1,at.x+10.5,at.z+6.0],
            [2,at.x+3.0,at.z+15.0]
        ]) {
            this.#spawnStatic(`Course guidance ${i+1}`,x,0.12,z,0.22,rail);
        }
    }

    #playCue(action) {
        try {
            Audio.playWav(`audio/protocol_${action}.wav`);
        } catch(error) {
            // An unavailable audio output device is not a gameplay failure.
            Logger.warn(`[FP Protocol] Optional sound unavailable: ${String(error)}`);
        }
    }

    #target() {
        if(this.#phase===0)return this.#beacons[0];
        if(this.#phase===1) {
            const body=this.#crate && Scene.get(this.#crate.index);
            return body ? body.position : this.#crateInitial;
        }
        if(this.#phase===2)return this.#beacons[1];
        if(this.#phase===3)return this.#beacons[2];
        return null;
    }

    #showMission() {
        const goal=this.#target();
        const pawn=Player.index>=0?Scene.get(Player.index):null;
        const p=pawn?.position;
        const distance=p && goal?
            Math.hypot(p.x-goal.x,p.z-goal.z):0;
        const objective=this.#completed
            ?`Training finished in ${this.#elapsed.toFixed(1)} seconds`
            :OBJECTIVES[this.#phase].instruction;
        // Script-owned UI composition: engine.ui only sees elements/properties.
        const stage=this.#completed?OBJECTIVES.length:this.#phase+1;
        const status=this.#completed?"SUCCESS":OBJECTIVES[this.#phase].name;
        const progress=Math.floor(stage*100/OBJECTIVES.length);
        const elapsed=Math.floor(this.#elapsed);
        const clock=String(Math.floor(elapsed/60))+":"+String(elapsed%60).padStart(2,"0");
        const text=(id,value,height,scale=2,color="#e1e9ffff")=>({
            id,type:"text",parent:"panel",props:{
                text:value,height,font_scale:scale,color
            }
        });
        UI.mount("mission",[
            {id:"panel",type:"panel",props:{
                width:540,height:219,x:12,y:12,padding:12,gap:4,
                background:"#0b1423d9"
            }},
            text("brand","NORTHSTAR / FIRST PERSON PROTOCOL",23,2,"#f6c753ff"),
            text("stage",String(stage)+" / "+OBJECTIVES.length+"  "+status,32,3),
            text("objective",objective,36,2),
            text("progress","PROGRESS  "+progress+"%",25,2,"#f6c753ff"),
            text("timer","TIME "+clock+"      TARGET "+Math.round(distance)+" M",26,2),
            text("controls",
                 this.#completed?"T RESTART     F10 EXIT":
                 "WASD MOVE   SHIFT RUN   SPACE JUMP   E USE   LMB FIRE   R RELOAD",
                 25,1,"#82cbdcff")
        ]);
    }

    #announce(text) {
        UI.setTitle("NorthStar: First Person Protocol");
        Logger.info(`[FP Protocol] ${text}`);
        this.#showMission();
    }

    #next() {
        this.#phase+=1;
        if(this.#phase===OBJECTIVES.length) {
            this.#completed=true;
            this.#playCue("complete");
            this.#announce(`COURSE COMPLETE: ${this.#elapsed.toFixed(1)} seconds`);
            return;
        }
        this.#playCue("checkpoint");
        this.#announce(`${this.#phase+1}/4: ${OBJECTIVES[this.#phase].instruction}`);
    }

    start() {
        const pawn=Scene.findPawn();
        if(!pawn || !Player.enabled || !Player.entityId)
            throw new Error("Training requires a possessed Pawn and Camera Rig");
        const at=pawn.position;
        if(![at.x,at.y,at.z].every(Number.isFinite))
            throw new Error("Pawn has an invalid spawn transform");
        this.#beacons=[
            this.#spawnBeacon("Navigation 01",at.x+8,at.z+1,0x32C9ED),
            this.#spawnBeacon("Navigation 02",at.x+13,at.z+12,0xFEA641),
            this.#spawnBeacon("Extraction",at.x-7,at.z+16,0x69E587)
        ];
        this.#dressCourse(at);
        this.#crateInitial={x:at.x+10.5,z:at.z+5.5};
        this.#crate=Scene.spawn({
            name:"Course Pushable Crate",
            position:{x:this.#crateInitial.x,y:1.25,z:this.#crateInitial.z},
            size:1,mass:6,color:0xEFC44B
        });
        this.#started=true;
        this.#phase=0;
        this.#elapsed=0;
        this.#hudClock=0;
        this.#completed=false;
        this.#lastRestart=false;
        this.#announce("1/4 SIGNAL: Reach the cyan beacon");
        this.#playCue("start");
    }

    update(dt) {
        if(!this.#started||Player.paused)return;
        const restart=Input.isDown(Keys.T);
        if(restart&&!this.#lastRestart) {
            this.#lastRestart=true;
            Session.restart();
            return;
        }
        this.#lastRestart=restart;
        if(this.#completed)return;
        if(Number.isFinite(dt)&&dt>0&&dt<1) {
            this.#elapsed+=dt;
            this.#hudClock+=dt;
        }
        const pawn=Player.index>=0?Scene.get(Player.index):null;
        if(!pawn)return;
        const p=pawn.position;
        const reached=beacon=>
            (p.x-beacon.x)**2+(p.z-beacon.z)**2<2.7 &&
            Math.abs(p.y-0.75)<4.5;
        if(this.#phase===0&&reached(this.#beacons[0])) {
            Scene.setColor(this.#beacons[0].light,0x69E587);
            this.#next();
        } else if(this.#phase===1&&this.#crate) {
            const body=Scene.get(this.#crate.index);
            if(body) {
                const pos=body.position;
                if(Math.hypot(pos.x-this.#crateInitial.x,
                              pos.z-this.#crateInitial.z)>=0.9) {
                    Scene.setColor(this.#crate.index,0x69E587);
                    this.#next();
                }
            }
        } else if(this.#phase===2&&reached(this.#beacons[1])) {
            Scene.setColor(this.#beacons[1].light,0x69E587);
            this.#next();
        } else if(this.#phase===3&&reached(this.#beacons[2])) {
            Scene.setColor(this.#beacons[2].light,0xF9D65C);
            this.#next();
        }
        if(this.#hudClock>=0.25) {
            this.#hudClock=0;
            this.#showMission();
        }
    }

    stop() {
        if(this.#started)
            Logger.info(`[FP Protocol] Session ended at ${this.#phase}/4`);
        UI.unmount("mission");
        this.#started=false;
        this.#beacons=[];
        this.#scenery=[];
        this.#crate=null;
    }
}

const course=new TrainingCourse();
new PistolWeapon().install();
Events.on("start",()=>course.start());
Events.on("update",dt=>course.update(dt));
Events.on("stop",()=>course.stop());
