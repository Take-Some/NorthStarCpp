    // Universal firearm gameplay in the ROOT @northstar/engine SDK.
    // Projects author weapon profiles and hit-specific gameplay; they never
    // duplicate firing cadence, YCD playback, recoil, VFX or brass spawning.
    class WeaponController extends Script {
        #profileProvider;
        #holderProvider;
        #controlsProvider;
        #aimProvider;
        #playerDriven;
        #onHit;
        #weapon=null;
        #profile=null;
        #ammo=0;
        #reserve=0;
        #cooldown=0;
        #reloadRemaining=0;
        #clipRemaining=0;
        #activeClip="";
        #attachedParent=-1;
        #attachmentMode="";
        #lastReload=false;
        #lastTrigger=false;
        #serial=0;
        #burstIndex=-1;
        #sinceShot=100;
        #status="UNEQUIPPED";
        #hudView=null;
        #hudSignature="";

        constructor({profile=null,holder=null,controls=null,aim=null,onHit=null}={}) {
            super();
            for(const [label,callback] of Object.entries({
                profile,holder,controls,aim,onHit
            })) {
                if(callback!==null && typeof callback!=="function")
                    throw new TypeError("WeaponController "+label+" must be a function");
            }
            this.#profileProvider=profile??(()=>Player.config?.weapon);
            this.#holderProvider=holder;
            this.#controlsProvider=controls;
            this.#aimProvider=aim;
            this.#playerDriven=profile===null && controls===null;
            this.#onHit=onHit;
        }

        get equipped() { return Boolean(this.#weapon?.alive); }
        get ammo() { return this.#ammo; }
        get reserveAmmo() { return this.#reserve; }
        get reloading() { return this.#reloadRemaining>0; }
        get status() { return this.#status; }
        get weaponIndex() { return this.equipped?this.#weapon.root.index:-1; }

        #cue(name,variation=0,gain=1) {
            if(!this.#profile?.audio)return 0;
            try {return Audio.play({asset:this.#profile.audio,clip:name,variation,gain});}
            catch(error) {
                Logger.warn("[engine.weapon] NAC "+name+": "+String(error));
                return 0;
            }
        }

        #animate(state,seconds=0) {
            const clips=this.#profile?.animations;
            if(!this.equipped || !clips?.dictionary || !clips[state])return false;
            const ref=clips.dictionary+"@"+clips[state];
            // Do not reset a one-frame idle clip on every update. A new
            // trigger still restarts p-fire so automatic fire stays animated.
            if(state==="idle" && this.#activeClip===ref)return true;
            try {
                Scene.playAnimation(this.weaponIndex,ref);
                this.#activeClip=ref;
                this.#clipRemaining=seconds;
                return true;
            } catch(error) {
                Logger.warn("[engine.weapon] YCD "+state+": "+String(error));
                return false;
            }
        }

        #hud() {
            const cfg=this.#profile;
            if(!cfg?.hudAsset)return;
            const signature=this.#ammo+":"+this.#reserve+":"+
                this.#status+":"+(this.reloading?"reload":"ready");
            if(signature===this.#hudSignature)return;
            this.#hudSignature=signature;
            try {
                if(!this.#hudView)
                    this.#hudView=UI.loadAsset(cfg.hudAsset,{
                        layer:cfg.hudLayer??"weapon"});
                this.#hudView.batch(()=>{
                    this.#hudView.setText("header",
                        String(cfg.displayName??"WEAPON").toUpperCase());
                    this.#hudView.setText("ammo",
                        String(this.#ammo).padStart(2,"0")+" / "+
                        String(this.#reserve).padStart(2,"0"));
                    this.#hudView.setText("state",
                        (this.reloading?"RELOADING":this.#status)+
                        (cfg.controlHint??(this.#playerDriven?
                          "   LMB FIRE  /  RMB AIM  /  R RELOAD":"")));
                });
                this.#hudView.mount();
            } catch(error) {
                Logger.warn("[engine.weapon] HUD: "+String(error));
            }
        }

        #follow() {
            if(!this.equipped || !this.#holderProvider)return;
            const holder=this.#holderProvider();
            const parent=holder?.alive?
                (holder.root?.index??holder.index??-1):-1;
            if(!Number.isSafeInteger(parent) || parent<0) {
                if(this.#attachedParent>=0) {
                    Scene.detachFromBone(this.weaponIndex);
                    this.#attachedParent=-1;
                    this.#attachmentMode="";
                }
                return;
            }
            // NPC attachments follow skeletal sockets, never the player's camera.
            const mode=this.#playerDriven &&
                Camera.mode()==="first_person"?"fps":"tps";
            if(parent===this.#attachedParent &&
               mode===this.#attachmentMode)return;
            if(this.#attachedParent>=0)
                Scene.detachFromBone(this.weaponIndex);
            const grip=this.#profile.grip;
            Scene.attachToBone(this.weaponIndex,parent,
                grip.bone,grip.offset??{},
                mode==="fps"?(grip.firstPersonRotation??grip.rotation??{}):
                    (grip.rotation??{}),
                mode==="fps"?"camera_forward":"socket");
            this.#attachedParent=parent;
            this.#attachmentMode=mode;
        }

        #recoil() {
            const r=this.#profile?.recoil??{};
            // Camera kick should not throw the iron sights off the target
            // as violently as an unaimed shot. All coefficients remain
            // weapon-authored; neither NPCs nor the renderer hardcode them.
            const ads=this.#playerDriven &&
                Camera.mode()==="first_person" && Input.mouseDown(1)?
                (r.aimDownSights??{}):{};
            // Deterministic multi-stage pattern, not per-shot random jitter.
            const pattern=r.shotPattern;
            const stage=Array.isArray(pattern)&&pattern.length?
                pattern[this.#burstIndex%pattern.length]:null;
            const pitchScale=stage?.pitchScale??1;
            const yawScale=stage?.yawScale??0;
            if(![pitchScale,yawScale].every(Number.isFinite) ||
               pitchScale<0 || pitchScale>3 || Math.abs(yawScale)>3)
                throw new RangeError("Invalid authored recoil pattern");
            const yawBase=ads.yawRadians??r.yawRadians??0;
            return {
                amplitude:ads.amplitude??r.amplitude??0.010,
                duration:ads.duration??r.duration??0.10,
                pitchRadians:(ads.pitchRadians??r.pitchRadians??0.045)*pitchScale,
                yawRadians:yawBase + yawScale*(r.patternYawRadians??0),
                recoverySeconds:ads.recoverySeconds??r.recoverySeconds??0.25,
                weaponBackMeters:ads.weaponBackMeters??r.weaponBackMeters??0,
                armImpulse:ads.armImpulse??r.armImpulse??0,
                snapSeconds:ads.snapSeconds??r.snapSeconds??.045,
                accentSeconds:ads.accentSeconds??r.accentSeconds??.035,
                settleSeconds:ads.settleSeconds??r.settleSeconds??.14,
                aftershock:ads.aftershock??r.aftershock??.18
            };
        }

        #fire() {
            if(this.#ammo<=0) {
                this.#status="EMPTY";
                this.#cooldown=0.24;
                this.#cue("empty");
                this.#animate("empty",0.167);
                this.#hud();
                return;
            }
            // Mechanical firing is authoritative even when an optional
            // attachment snapshot/VFX provider has not become ready.
            --this.#ammo;
            ++this.#serial;
            this.#burstIndex=this.#sinceShot>.36?0:this.#burstIndex+1;
            this.#sinceShot=0;
            this.#cooldown=1/this.#profile.roundsPerSecond;
            this.#status="FIRED";
            this.#animate("fire",this.#profile.fireAnimationSeconds??0.17);
            this.#cue("fire_body",0,this.#profile.fireGain??1);
            let shot=null;
            const holder=this.#holderProvider?.();
            // The possessed Pawn owns the Jolt character capsule; the
            // attached skeletal visual is a different scene entity. Ignoring
            // only that visual makes the camera ray strike our own capsule.
            const ownerIndex=this.#playerDriven && Player.index>=0?
                Player.index:(holder?.alive?
                    (holder.root?.index??holder.index??-1):-1);
            const aim=this.#aimProvider?.()??{};
            try {
                shot=WeaponBallistics.solve({
                    weaponIndex:this.weaponIndex,
                    muzzleLocal:this.#profile.muzzleLocal,
                    ejectionLocal:this.#profile.ejectionLocal,
                    barrelAxisLocal:this.#profile.barrelAxisLocal,
                    range:this.#profile.range,
                    radius:this.#profile.sweepRadius,
                    adsRadius:this.#profile.aimSweepRadius??0.006,
                    eye:aim.eye??null,
                    aimDirection:aim.direction??null,
                    mode:aim.mode??(this.#playerDriven?"auto":"barrel"),
                    ignoreIndex:ownerIndex
                });
            } catch(error) {
                Logger.warn("[engine.weapon] Ballistics: "+String(error));
            }
            // Only the locally owned camera receives kick by default.
            // World NPC weapons still emit VFX, damage and physical casings.
            const cameraRecoil=this.#profile.cameraRecoil??this.#playerDriven;
            if(shot) {
                try {
                    WeaponEffects.fire({
                        shot,
                        casingType:this.#profile.casingType??"pistol",
                        casingPrefab:this.#profile.casingPrefab??null,
                        shellImpulse:this.#profile.shellImpulse??1,
                        casingAudio:this.#profile.casingAudio??
                            this.#profile.audio??null,
                        casingMaterial:this.#profile.casingMaterial??
                            {friction:.30,restitution:.42},
                        emitCasing:this.#profile.emitCasing??true,
                        recoil:cameraRecoil?this.#recoil():null
                    });
                } catch(error) {
                    Logger.warn("[engine.weapon] Effects: "+String(error));
                    if(cameraRecoil)WeaponEffects.kick(this.#recoil());
                }
            } else {
                if(cameraRecoil)WeaponEffects.kick(this.#recoil());
                // No eye-origin VFX/ballistics fallback: the authored
                // physical barrel socket must become available.
                if(this.#serial<=3 || this.#serial%20===0)
                    Logger.warn("[engine.weapon] Missing weapon socket snapshot; "+
                        "shot has recoil/animation but no projectile or muzzle VFX");
            }
            if(shot?.hit && !shot.blocked && shot.hit.index!==ownerIndex) {
                const target=shot.hit.index>=0?Scene.get(shot.hit.index):null;
                if(target && Number(target.mass)>0 &&
                   Number.isFinite(this.#profile.impactImpulse)) {
                    const direction=shot.direction;
                    Physics.applyImpulse(shot.hit.index,{
                        x:direction.x*this.#profile.impactImpulse,
                        y:direction.y*this.#profile.impactImpulse,
                        z:direction.z*this.#profile.impactImpulse
                    });
                }
                this.#status="HIT";
                this.#onHit?.({hit:shot.hit,shot,target,weapon:this});
            }
            this.#hud();
        }

        /** Explicit fire for AI, cinematic sequences and scripted turrets. */
        fire() {
            if(!this.equipped || this.reloading || this.#cooldown>0)return false;
            this.#fire();
            return true;
        }

        reload() {
            if(!this.equipped || this.reloading ||
               this.#ammo>=this.#profile.magazineSize ||
               this.#reserve<=0)return false;
            this.#reloadRemaining=this.#profile.reloadSeconds;
            this.#status="RELOADING";
            this.#cue("reload");
            this.#animate("reload",this.#reloadRemaining);
            this.#hud();
            return true;
        }

        start() {
            const cfg=this.#profileProvider();
            if(!cfg?.enabled)return;
            // Legacy player-owned controllers still require possession.
            // Explicit profiles/holders may belong to an NPC or a free object.
            if(this.#playerDriven && !this.#holderProvider &&
               (Player.index<0 || !Player.entityId))
                throw new Error("Player weapon requires a possessed Pawn");
            for(const key of ["magazineSize","roundsPerSecond",
                               "reloadSeconds","range","sweepRadius"]) {
                if(!Number.isFinite(cfg[key]) || cfg[key]<=0)
                    throw new RangeError("Invalid weapon profile: "+key);
            }
            if(!Number.isSafeInteger(cfg.magazineSize) ||
               !Number.isSafeInteger(cfg.reserveAmmo) || cfg.reserveAmmo<0 ||
               !["semi","auto"].includes(cfg.fireMode??"semi"))
                throw new RangeError("Weapon magazine, reserve or fireMode invalid");
            if(typeof cfg.prefab!=="string" || !cfg.prefab ||
               (this.#holderProvider && !cfg.grip?.bone) ||
               !cfg.muzzleLocal ||
               (cfg.emitCasing!==false && !cfg.ejectionLocal))
                throw new TypeError("Weapon profile requires prefab, muzzleLocal, "+
                    "grip for attached weapons and ejectionLocal when casing is enabled");
            this.#profile=cfg;
            this.#ammo=cfg.magazineSize;
            this.#reserve=cfg.reserveAmmo;
            this.#cooldown=this.#reloadRemaining=this.#clipRemaining=0;
            this.#activeClip=this.#hudSignature="";
            this.#serial=0;
            this.#burstIndex=-1;
            this.#sinceShot=100;
            this.#lastReload=this.#lastTrigger=false;
            this.#attachedParent=-1;
            this.#attachmentMode="";
            const holder=this.#holderProvider?.();
            const holderIndex=holder?.alive?
                (holder.root?.index??holder.index??-1):-1;
            const position=cfg.position??holder?.root?.snapshot?.position??
                holder?.snapshot?.position??
                (holderIndex>=0?Scene.get(holderIndex)?.position:null)??
                (this.#playerDriven && Player.index>=0?Player.eye():null);
            if(!position)throw new Error(
                "World weapon requires profile.position or a living holder");
            Contracts.vector(position,"Weapon spawn position");
            // Equipped visual is socket-driven: physical world prefab keeps real mass.
            this.#weapon=Factory.prefab(cfg.prefab,{
                position,physics:"visual"});
            this.#follow();
            this.#animate("idle");
            this.#cue("equip");
            this.#status="READY";
            this.#hud();
            Logger.info("[engine.weapon] Equipped "+
                String(cfg.displayName??cfg.prefab));
        }

        update(dt) {
            if(!this.equipped || Player.paused ||
               !Number.isFinite(dt) || dt<0 || dt>0.5)return;
            this.#follow();
            this.#sinceShot=Math.min(100,this.#sinceShot+dt);
            // Advance only once from the weapon controller. The project
            // visual samples this channel but does not advance it.
            WeaponEffects.advanceMotion?.(dt);
            // Contact dispatch is owned by Physics, once per engine tick.
            if(this.#clipRemaining>0 && !this.reloading) {
                this.#clipRemaining=Math.max(0,this.#clipRemaining-dt);
                if(this.#clipRemaining===0)this.#animate("idle");
            }
            this.#cooldown=Math.max(0,this.#cooldown-dt);
            const wasReloading=this.reloading;
            this.#reloadRemaining=Math.max(0,this.#reloadRemaining-dt);
            if(wasReloading && !this.reloading) {
                const loaded=Math.min(
                    this.#profile.magazineSize-this.#ammo,this.#reserve);
                this.#ammo+=loaded;
                this.#reserve-=loaded;
                this.#status="READY";
                this.#animate("idle");
                this.#hud();
            }
            const controls=this.#controlsProvider?
                Contracts.options(this.#controlsProvider(),"Weapon controls"):
                (this.#playerDriven?
                 {reload:Input.isDown(NorthStar.modules.engine.Keys.R),
                  trigger:Input.mouseDown(0)}:
                 {reload:false,trigger:false});
            if(typeof controls.reload!=="boolean" ||
               typeof controls.trigger!=="boolean")
                throw new TypeError("Weapon controls require boolean trigger/reload");
            const reloadDown=controls.reload;
            if(reloadDown && !this.#lastReload)this.reload();
            this.#lastReload=reloadDown;
            const trigger=controls.trigger;
            if(trigger && !this.#lastTrigger && !this.#ammo &&
               this.#reserve>0)this.reload();
            const mode=this.#profile.fireMode??"semi";
            if(trigger && !this.reloading && this.#cooldown<=0 &&
               (mode==="auto" || !this.#lastTrigger))this.fire();
            this.#lastTrigger=trigger;
        }

        stop() {
            if(this.#weapon) {
                if(this.#attachedParent>=0 && this.#weapon.alive)
                    Scene.detachFromBone(this.#weapon.root.index);
                if(this.#weapon.alive)this.#weapon.destroy();
                this.#weapon=null;
            }
            this.#hudView?.dispose();
            this.#hudView=null;
            this.#hudSignature="";
            this.#attachedParent=-1;
            this.#attachmentMode="";
            this.#profile=null;
            this.#burstIndex=-1;
            this.#sinceShot=100;
            WeaponEffects.resetMotion?.();
            this.#status="UNEQUIPPED";
        }
    }
