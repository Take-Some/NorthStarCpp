// Abby Pistol Demo: standalone, project-owned first-person weapon logic.
// Native only provides generic Player/Scene/Physics/Camera/Audio/UI.
import { Script, Player, Prefab, Scene, Input, Keys, UI, Camera, Physics, Audio, Logger } from "@northstar/engine";

// Character visuals are optional and never implement player possession.
// Any script can instantiate prefab assets independently of this controller.
class CharacterVisual extends Script {
    // One prefab, one entity identity and one animation timeline for all views.
    // First-person arm isolation is only a render presentation of this actor.
    #instance=null;
    #ownsInstance=false;
    #inheritsPawnPose=false;
    #previousYaw=null;
    #previousPosition=null;
    #activeBodyClip="";
    #mode="";

    get holder() {
        return this.#instance?.alive ? this.#instance : null;
    }

    #update(dt=0) {
        const definition=Player.config?.character;
        if(!Player.enabled || Player.index<0 || !definition?.prefab)return;
        const pawn=Scene.findPawn();
        if(!pawn)return;
        if(!this.#instance || !this.#instance.alive) {
            const visualName=definition.entityName??"SkeletalMesh";
            const authoredRecord=Scene.findByName(visualName);
            // Authored visual owns its identity; do not silently spawn a
            // duplicate when a shipped JS API bundle is out of date.
            if(authoredRecord && typeof Scene.handleByName!=="function")
                throw new Error("Missing Scene.handleByName for "+visualName);
            const authored=authoredRecord?Scene.handleByName(visualName):null;
            if(authoredRecord && !authored?.alive)
                throw new Error("Authored visual handle is unavailable: "+visualName);
            if(authored?.alive) {
                const root=authored;
                this.#instance={root,get alive(){return root.alive;}};
                this.#ownsInstance=false;
                // Native Jolt already follows the Pawn for authored child
                // SkeletalMesh entities; do not write the same position again.
                this.#inheritsPawnPose=String(authoredRecord.parent)===
                    String(pawn.id);
            } else {
                // Backward compatibility with scenes without authored visuals.
                this.#instance=Prefab.instantiate(definition.prefab,{
                    position:pawn.position
                });
                this.#ownsInstance=true;
                this.#inheritsPawnPose=false;
            }
            this.#previousYaw=null;
            this.#activeBodyClip="";
            this.#previousPosition=null;
            this.#mode="";
        }
        const mode=Camera.mode();
        if(mode!==this.#mode) {
            Scene.setVisualMode(this.#instance.root.index,
                mode==="first_person" ? "first_person_arms" : "world",
                definition.firstPersonViewmodel);
            this.#mode=mode;
        }
        // Keep the authored scene transform at the Pawn. The viewport
        // presents only this instance's skinned arms near the camera in FPS,
        // never teleports or creates an unrelated model/entity.
        const yaw=Camera.angles().yaw;
        if(!this.#inheritsPawnPose)
            this.#instance.root.setPosition(pawn.position);
        // Only rotate on a heading change. This avoids redundant host calls
        // while standing still with a 377k-vertex animated skeletal model.
        if(this.#previousYaw===null || Math.abs(yaw-this.#previousYaw)>1e-5) {
            this.#instance.root.setRotation({x:0,y:yaw*180/Math.PI,z:0});
            this.#previousYaw=yaw;
        }
        const old=this.#previousPosition;
        const speed=old && dt>0 ?
            Math.hypot(pawn.position.x-old.x,
                pawn.position.z-old.z)/dt:0;
        const state=speed>0.2 && speed<35?"walk":"idle";
        const animation=definition.resources?.animations?.find(
            entry=>entry.name===state);
        const clip=animation?animation.source+"@"+animation.clip:"";
        if(clip && clip!==this.#activeBodyClip) {
            Scene.playAnimation(this.#instance.root.index,clip);
            this.#activeBodyClip=clip;
        }
        this.#previousPosition={...pawn.position};
    }

    start() {this.#update();}
    update(dt) {
        if(!Player.paused && Number.isFinite(dt) && dt>=0 && dt<=0.5)
            this.#update(dt);
    }
    stop() {
        if(this.#instance) {
            if(this.#instance.alive)
                Scene.setVisualMode(this.#instance.root.index,"world");
            if(this.#ownsInstance)this.#instance.destroy();
        }
        this.#instance=null;
        this.#ownsInstance=false;
        this.#inheritsPawnPose=false;
        this.#previousYaw=null;
        this.#mode="";
        this.#previousPosition=null;
        this.#activeBodyClip="";
    }
}

class PistolWeapon extends Script {
    #instance=null;
    #character;
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
    #clipEnd=0;
    #activeClip="";
    #attachmentParent=-1;
    #attachmentMode="";

    constructor(character) {
        super();
        this.#character=character;
    }

    #animate(name, seconds=0) {
        if(!this.#instance?.alive || !this.#config?.animations)return;
        const animation=this.#config.animations;
        const clip=animation[name];
        if(!clip)return;
        const chosen=animation.dictionary+"@"+clip;
        if(chosen===this.#activeClip && seconds<=0)return;
        try {
            Scene.playAnimation(this.#instance.root.index, chosen);
            this.#activeClip=chosen;
            this.#clipEnd=seconds;
        }catch(error) {
            Logger.warn("[Pistol] Unable to play "+chosen+": "+String(error));
        }
    }

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
                text:String(this.#config?.displayName??"WEAPON").toUpperCase(),
                height:24,font_scale:2,color:"#f6c753ff"}},
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
        if(!this.#instance?.alive)return;
        const holder=this.#character.holder;
        if(!holder?.alive) {
            if(this.#attachmentParent>=0)
                Scene.detachFromBone(this.#instance.root.index);
            this.#attachmentParent=-1;
            this.#attachmentMode="";
            return;
        }
        const mode=Camera.mode()==="first_person" ? "camera_forward" : "socket";
        // Same character and gun instances, only the presentation mode changes.
        if(this.#attachmentParent===holder.root.index &&
           this.#attachmentMode===mode)return;
        if(this.#attachmentParent>=0)
            Scene.detachFromBone(this.#instance.root.index);
        const grip=this.#config.grip;
        Scene.attachToBone(this.#instance.root.index,holder.root.index,
            grip.bone,grip.offset,
            mode==="camera_forward" ?
                (grip.firstPersonRotation??grip.rotation):grip.rotation,
            mode);
        this.#attachmentParent=holder.root.index;
        this.#attachmentMode=mode;
    }

    #fire() {
        if(this.#ammo<=0) {
            this.#status="EMPTY";
            this.#cooldown=0.24;
            this.#cue("empty");
            this.#animate("empty",0.167);
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
                if(target?.name?.startsWith("Target "))
                    Scene.setColor(hit.index,0x67D6A6);
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
        this.#animate("fire",0.167);
        Camera.shake(0.026,0.08);
    }

    #beginReload() {
        if(this.#reload>0 || this.#ammo>=this.#config.magazineSize ||
           this.#reserve<=0)return;
        this.#reload=this.#config.reloadSeconds;
        this.#status="RELOADING";
        this.#cue("reload");
        this.#animate("reload",this.#reload);
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
        this.#animate("idle");
        this.#cue("equip");
        this.#status="READY";
        this.#hud();
        Logger.info("[Weapon] Equipped. Magazine="+this.#ammo+
                    ", reserve="+this.#reserve);
    }

    update(dt) {
        if(!this.#config || !this.#instance?.alive)return;
        if(Player.paused)return;
        if(!Number.isFinite(dt) || dt<0 || dt>0.5)return;
        this.#follow(dt);
        if(this.#clipEnd>0) {
            this.#clipEnd=Math.max(0,this.#clipEnd-dt);
            if(this.#clipEnd===0)this.#animate("idle");
        }
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
            if(this.#attachmentParent>=0)
                Scene.detachFromBone(this.#instance.root.index);
            this.#attachmentParent=-1;
            this.#attachmentMode="";
            this.#instance.destroy();
            this.#instance=null;
        }
        UI.unmount("weapon");
        this.#config=null;
        this.#lastTrigger=false;this.#lastReload=false;
    }
}

const character=new CharacterVisual();
character.install();
new PistolWeapon(character).install();
