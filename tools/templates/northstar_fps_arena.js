// NorthStar FPS Arena — project gameplay, not engine code.
// Copyright (C) 2026 Kayla's Systems — Kayla Verner.
(() => {
    "use strict";
    const keys = {W:87,A:65,S:83,D:68,JUMP:32,RELOAD:82,ESC:27,RESTART:13,EXIT:121};
    const settings = Object.freeze({
        sensitivity:0.0024, maxPitch:1.4, eyeHeight:1.45,
        magazine:12, reserve:60, hitColor:0x3cffb5,
        fireInterval:0.16, reloadSeconds:1.25, weaponRange:55,
        damagePerHazard:20, damageCooldown:0.8,
        movingFrequency:1.7, movingAmplitude:0.9,
        // Add project-owned WAV files under assets/audio and set these paths.
        shotSound:null, reloadSound:null
    });
    const down = key => engine.input.isDown(key);
    const controller = () => {
        for (let i = 0; i < 4; ++i) {
            const pad = engine.input.gamepad(i);
            if (pad) return pad;
        }
        return null;
    };
    const clamp = (value,min,max) => Math.max(min,Math.min(max,value));
    const objects = new Map(), targets = new Map(), hazards = [], movers = [];
    let playerIndex=-1, yaw=0,pitch=0;
    let health=100,ammo=settings.magazine,reserve=settings.reserve;
    let hits=0,shots=0,shotCooldown=0,reloadTimer=0,damageCooldown=0,elapsed=0;
    let paused=false,priorJump=false,priorReload=false,priorPause=false,priorRestart=false;
    let displayTimer=0;
    const won = () => targets.size>0 && hits===targets.size;
    const alive = () => health>0;

    function refreshTitle() {
        const status=paused ? " | PAUSED" : !alive() ? " | GAME OVER (ENTER TO RESTART)"
            : won() ? " | COMPLETE (ENTER TO RESTART)" : "";
        engine.ui.setTitle(`NorthStar FPS Arena | ${hits}/${targets.size} hits | HP ${health} | Ammo ${ammo}/${reserve}${reloadTimer>0?" | RELOADING":""}${status}`);
    }
    function collectScene() {
        for(let start=0;;start+=256) {
            const page=engine.scene.objects(start,256);
            for(const object of page) {
                objects.set(object.index,object);
                if(object.player) playerIndex=object.index;
                if(object.name.startsWith("Target ") &&
                    !object.name.startsWith("Target Base ")) {
                    targets.set(object.index,false);
                    if(object.name.startsWith("Target Moving ")) {
                        movers.push({index:object.index,origin:{...object.position},
                            phase:(movers.length)*1.9});
                    }
                }
                if(object.name.startsWith("Hazard ")) hazards.push(object);
            }
            if(page.length<256) return;
        }
    }
    function fire() {
        if(!alive() || shotCooldown>0 || reloadTimer>0 || ammo===0) return;
        --ammo; ++shots; shotCooldown=settings.fireInterval;
        engine.camera.shake(0.012,0.12);
        if(settings.shotSound) engine.audio.play(settings.shotSound);
        const eye=engine.player.eye();
        const aim={x:Math.sin(yaw)*Math.cos(pitch),
            y:Math.sin(pitch),z:Math.cos(yaw)*Math.cos(pitch)};
        const hit=engine.physics.sweepSphere(
            eye.x,eye.y,eye.z,
            eye.x+aim.x*settings.weaponRange,
            eye.y+aim.y*settings.weaponRange,
            eye.z+aim.z*settings.weaponRange,0.025,playerIndex);
        if(hit && targets.has(hit.index) && !targets.get(hit.index)) {
            targets.set(hit.index,true);
            ++hits;
            engine.scene.setColor(hit.index,settings.hitColor);
            engine.logger.info("Arena target hit: "+objects.get(hit.index).name);
        }
    }
    function reload() {
        if(reloadTimer>0 || ammo===settings.magazine || reserve<=0) return;
        reloadTimer=settings.reloadSeconds;
        if(settings.reloadSound) engine.audio.play(settings.reloadSound);
    }
    function movement(pad) {
        const buttons=pad ? pad.buttons : 0;
        const dx=Number(down(keys.D))-Number(down(keys.A)) +
            (pad ? pad.leftX+Number(Boolean(buttons & 0x0008)) -
                Number(Boolean(buttons & 0x0004)) : 0);
        const dz=Number(down(keys.W))-Number(down(keys.S)) +
            (pad ? pad.leftY+Number(Boolean(buttons & 0x0001)) -
                Number(Boolean(buttons & 0x0002)) : 0);
        const scale=Math.max(1,Math.hypot(dx,dz));
        engine.player.move((dx*Math.cos(yaw)+dz*Math.sin(yaw))/scale,
            (dz*Math.cos(yaw)-dx*Math.sin(yaw))/scale);
    }
    function animateTargets() {
        for(const target of movers) {
            if(targets.get(target.index)) continue;
            const x=target.origin.x+
                Math.sin(elapsed*settings.movingFrequency+target.phase)*settings.movingAmplitude;
            engine.scene.moveStatic(target.index,x,target.origin.y,target.origin.z);
        }
    }
    function checkHazards() {
        if(damageCooldown>0) return;
        const eye=engine.player.eye();
        const player={x:eye.x,y:eye.y-settings.eyeHeight,z:eye.z};
        for(const hazard of hazards) {
            const radius=hazard.size*0.5+0.90;
            if(Math.abs(player.y-hazard.position.y)<1.4 &&
               Math.abs(player.x-hazard.position.x)<radius &&
               Math.abs(player.z-hazard.position.z)<radius) {
                health=Math.max(0,health-settings.damagePerHazard);
                damageCooldown=settings.damageCooldown;
                engine.logger.warn("Hazard damage; HP="+health);
                break;
            }
        }
    }
    engine.on("start",()=>{
        collectScene();
        if(playerIndex<0 || targets.size===0)
            throw Error("Arena requires a player and targets");
        engine.player.setEyeHeight(settings.eyeHeight);
        engine.logger.info("Script-owned arena started: "+targets.size+" targets");
        refreshTitle();
    });
    engine.on("update", rawDt=>{
        const dt=clamp(rawDt,0,0.05);
        const pad=controller();
        const buttons=pad ? pad.buttons : 0;
        const pause=down(keys.ESC) || Boolean(buttons & 0x0010);
        if(pause && !priorPause) {
            paused=!paused;
            engine.session.pause(paused);
            refreshTitle();
        }
        priorPause=pause;
        if(down(keys.EXIT) || Boolean(buttons & 0x0020)) engine.session.quit();
        const restart=down(keys.RESTART);
        if(restart && !priorRestart && (!alive() || won()))
            engine.session.restart();
        priorRestart=restart;
        if(paused || !alive() || won()) {
            engine.player.move(0,0);
            return;
        }
        elapsed+=dt;
        shotCooldown=Math.max(0,shotCooldown-dt);
        damageCooldown=Math.max(0,damageCooldown-dt);
        if(reloadTimer>0) {
            reloadTimer=Math.max(0,reloadTimer-dt);
            if(reloadTimer===0) {
                const count=Math.min(settings.magazine-ammo,reserve);
                ammo+=count; reserve-=count;
            }
        }
        const pointer=engine.input.pointerDelta();
        yaw+=pointer.x*settings.sensitivity +
            (pad ? pad.rightX*2.5*dt : 0);
        pitch=clamp(pitch-pointer.y*settings.sensitivity +
            (pad ? pad.rightY*2.1*dt : 0),-settings.maxPitch,settings.maxPitch);
        if(Math.abs(yaw)>Math.PI) yaw%=2*Math.PI;
        engine.camera.setAngles(yaw,pitch);
        engine.camera.setZoom(engine.input.mouseDown(1)?1:0);
        movement(pad);
        const jump=down(keys.JUMP) || Boolean(buttons & 0x1000);
        if(jump && !priorJump) engine.player.jump();
        priorJump=jump;
        const reloadKey=down(keys.RELOAD) || Boolean(buttons & 0x4000);
        if(reloadKey && !priorReload) reload();
        priorReload=reloadKey;
        if(engine.input.mouseDown(0) ||
           (pad && pad.rightTrigger>0.35)) fire();
        animateTargets();
        checkHazards();
        displayTimer+=dt;
        if(displayTimer>=0.15 || !alive() || won()) {
            displayTimer=0;
            refreshTitle();
        }
    });
    engine.on("stop",()=>engine.logger.info("Script-owned arena stopped"));
})();
