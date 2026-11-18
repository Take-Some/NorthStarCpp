// Abby demo: project owns character presentation and target rules.
// All weapon operations are implemented by the common engine WeaponController.
import { Script, Factory, Player, Scene, Input, Camera, Logger, WeaponEffects, Sky } from "@northstar/engine";

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
    #activePlaybackRate=1;
    #smoothedSpeed=0;
    #gait="idle";
    #mode="";
    #stance="";

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
                this.#instance=Factory.prefab(definition.prefab,{
                    position:pawn.position
                });
                this.#ownsInstance=true;
                this.#inheritsPawnPose=false;
            }
            this.#previousYaw=null;
            this.#activeBodyClip="";
            this.#activePlaybackRate=1;
            this.#smoothedSpeed=0;
            this.#gait="idle";
            this.#previousPosition=null;
            this.#mode="";
        }
        const mode=Camera.mode();
        // ADS is authored by the game's equipped-weapon profile, not
        // a special camera/character branch inside the renderer.
        const weaponProfile=Player.config?.weapon;
        const aiming=mode==="first_person" &&
            weaponProfile?.enabled===true && Input.mouseDown(1);
        const motion=mode==="first_person" && weaponProfile?.enabled?
            (WeaponEffects.viewMotion?.()??{backMeters:0,armImpulse:0}):
            {backMeters:0,armImpulse:0};
        const presentation=aiming ?
            {...definition.firstPersonViewmodel,
             ...(weaponProfile?.enabled ? weaponProfile.grip?.aimDownSights : {}),
             ads:true,recoilBack:motion.backMeters,recoilArm:motion.armImpulse} :
            {...definition.firstPersonViewmodel,
             recoilBack:motion.backMeters,recoilArm:motion.armImpulse};
        const presentationKey=mode+":"+Number(aiming)+":"+
            motion.backMeters.toFixed(4)+":"+motion.armImpulse.toFixed(3);
        if(presentationKey!==this.#mode) {
            // ADS changes the IK presentation but retains a single skinned
            // body and its real r_hand_prop_attachment pistol socket.
            Scene.setVisualMode(this.#instance.root.index,
                mode==="first_person" ? "first_person_body" : "world",
                presentation);
            this.#mode=presentationKey;
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
        const rawSpeed=old && dt>0 ?
            Math.hypot(pawn.position.x-old.x,
                pawn.position.z-old.z)/dt:0;
        // Use actual horizontal Pawn velocity, not a key-state guess.
        // Smooth small Jolt/variable-frame fluctuations without hiding
        // a true stop or teleport. Speeds are metres per second.
        const tuning=definition.locomotion??{};
        const validSpeed=Number.isFinite(rawSpeed) && rawSpeed>=0 &&
            rawSpeed<35 ? rawSpeed : 0;
        if(rawSpeed>=35)this.#smoothedSpeed=0;
        const smoothSeconds=Math.max(0.01,tuning.smoothingSeconds??0.12);
        const alpha=dt>0 ? 1-Math.exp(-dt/smoothSeconds) : 0;
        this.#smoothedSpeed+=(validSpeed-this.#smoothedSpeed)*alpha;
        const speed=this.#smoothedSpeed;
        const hysteresis=tuning.hysteresis??0.18;
        const moving=tuning.movementThreshold??0.20;
        let state="walk";
        if(speed<(this.#gait==="idle"?moving*1.2:moving*0.8))
            state="idle";
        else if(speed>=(tuning.runToSprint??3.695)+
                (this.#gait==="sprint"?-hysteresis:hysteresis))
            state="sprint";
        else if(speed>=(tuning.walkToRun??2.215)+
                (this.#gait==="run"||this.#gait==="sprint"?-hysteresis:hysteresis))
            state="run";
        this.#gait=state;
        // Explicit project-owned weapon stance; attachment-driven IK uses
        // different FPS/TPS presentation targets without duplicating Abby.
        const equipped=Boolean(Player.config?.weapon?.enabled);
        const stance=equipped ?
            (mode==="first_person" ? "armed_first_person_pistol" :
                "armed_third_person_pistol") : "unarmed";
        if(stance!==this.#stance) {
            this.#stance=stance;
            Logger.info("[Character] stance="+stance);
        }
        const animation=definition.resources?.animations?.find(
            entry=>entry.name===state);
        const clip=animation?animation.source+"@"+animation.clip:"";
        const referenceSpeed=state==="sprint"?(tuning.sprintSpeed??4.44):
            state==="run"?(tuning.runSpeed??2.95):(tuning.walkSpeed??1.48);
        const playbackRate=state==="idle" ? 1 :
            Math.max(tuning.minPlaybackRate??0.65,
                Math.min(tuning.maxPlaybackRate??1.5,
                    speed/Math.max(referenceSpeed,0.1)));
        // Updating speed on the same clip retains its gait phase.
        if(clip && (clip!==this.#activeBodyClip ||
                    Math.abs(playbackRate-this.#activePlaybackRate)>0.03)) {
            Scene.playAnimation(this.#instance.root.index,clip,playbackRate);
            this.#activeBodyClip=clip;
            this.#activePlaybackRate=playbackRate;
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
        this.#activePlaybackRate=1;
        this.#smoothedSpeed=0;
        this.#gait="idle";
        this.#stance="";
    }
    onDestroy() {
        // Also handles a failed start before the native "stop" event.
        this.stop();
    }
}

// Only author project-specific hit reactions here. Ammunition, firing,
// YCD fire/reload playback, physical casings, VFX and camera recoil are
// implemented by the replaceable root @northstar/engine weapon service.
// The world owns daylight, clouds and celestial motion via the shared API.
// No per-level Vulkan/D3D12 workaround or second skydome entity.
Sky.configure({
    enabled:true,
    timeOfDay:17.5,
    dayLengthSeconds:1800,
    latitudeDegrees:42,
    dayOfYear:172,
    cloudCoverage:.42,
    cloudSpeed:.025,
    sun:true,moon:true,stars:true,clouds:true
});
const character=Factory.script(CharacterVisual);
Factory.weapon({
    holder:()=>character.holder,
    onHit:({hit,target})=>{
        if(target?.name?.startsWith("Target "))
            Scene.setColor(hit.index,0x67D6A6);
    }
}).install();
