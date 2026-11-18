    class WeaponEffects {
        static #shots=0;
        static #recoilMotion=new WeaponRecoilMotion();
        static advanceMotion(dt) {return WeaponEffects.#recoilMotion.advance(dt);}
        static viewMotion() {return WeaponEffects.#recoilMotion.sample();}
        static resetMotion() {WeaponEffects.#recoilMotion.reset();}
        static #casingFailures=0;
        // Registry only observes Jolt contact events; the actual shell remains
        // a persistent dynamic Scene entity, never a visual-only particle.
        static #shellContacts=new Map();
        static #contactClock=0;
        static #contactSounds=0;
        static #rollSounds=0;
        static #contactSubscription=null;
        static #contactStop=null;
        static #ensureContacts() {
            if (WeaponEffects.#contactSubscription || typeof engine.on!=="function")
                return;
            WeaponEffects.#contactSubscription=Physics.onContacts((events,dt)=>
                WeaponEffects.#processContacts(events,dt));
            WeaponEffects.#contactStop=Events.on("stop",()=>{
                WeaponEffects.#shellContacts.clear();
                WeaponEffects.#contactSubscription=null;
                WeaponEffects.#contactStop?.();
                WeaponEffects.#contactStop=null;
            });
        }
        static #playShell(shell,index,clip,variation,gain) {
            // Each impact is emitted at the *actual* Jolt casing position.
            // In an audio ABI-1 environment the 2D NAC fallback still works.
            if(typeof engine.scene?.entities?.objects==="function" &&
               typeof Audio.playNacAt==="function") {
                const position=Scene.get(index)?.position;
                if(position)
                    return Audio.playNacAt(shell.audio,clip,
                        position,variation,gain);
            }
            return Audio.playNac(shell.audio,clip,variation,gain);
        }

        static #processContacts(events,dt) {
            if(!Number.isFinite(dt)||dt<=0||dt>.5)return;
            WeaponEffects.#contactClock+=dt;
            for(const event of events) {
                if(event.phase!==0 || event.response!==2)continue;
                const index=WeaponEffects.#shellContacts.has(event.first)?
                    event.first:event.second;
                const shell=WeaponEffects.#shellContacts.get(index);
                if(!shell)continue;
                const time=WeaponEffects.#contactClock;
                if(time-shell.lastContact<.075)continue;
                shell.lastContact=time;
                ++shell.contacts;
                // The contact stream identifies real rigid-body collisions,
                // including the native ground (-1). An initial impact rings
                // strongly; subsequent bounces use lighter NAC variations.
                const category=shell.contacts===1?"shell_contact_hard":
                    shell.contacts===2?"shell_contact_medium":
                    shell.contacts===3?"shell_contact_small":
                    "shell_contact_soft";
                const gain=shell.contacts===1?.48:
                    shell.contacts===2?.34:shell.contacts===3?.24:.14;
                try {
                    WeaponEffects.#playShell(shell,index,category,
                        (shell.serial+shell.contacts)%7,gain);
                    ++WeaponEffects.#contactSounds;
                } catch(error) {
                    if(shell.contacts===1)
                        Logger.warn("[engine.weaponfx] shell impact NAC: "+String(error));
                }
                // Rolling is a separate sound layer; only after multiple
                // actual ground/bodies contacts, not at spawn.
                if(!shell.rolled && shell.contacts>=3) {
                    shell.rolled=true;
                    try {
                        WeaponEffects.#playShell(shell,index,"shell_roll",
                            shell.serial%3,.14);
                        ++WeaponEffects.#rollSounds;
                    } catch(error) {
                        Logger.warn("[engine.weaponfx] shell roll NAC: "+String(error));
                    }
                }
            }
        }

        /** Compatibility for tools without the engine event bus.
         *  Runtime contacts are consumed only by Physics.onContacts. */
        static update(dt) {
            if (WeaponEffects.#contactSubscription) return;
            if (typeof engine.on==="function") return;
            WeaponEffects.#processContacts(Physics.pollContacts(256),dt);
        }

        /** Independent recoil: even missing muzzle geometry or VFX must not suppress it. */
        static kick({amplitude = 0.010, duration = 0.10,
                     pitchRadians = 0.045, yawRadians = 0,
                     recoverySeconds = 0.25,
                     weaponBackMeters=0,armImpulse=0,
                     snapSeconds=.045,accentSeconds=.035,
                     settleSeconds=.14,aftershock=.18} = {}) {
            const values = [amplitude,duration,pitchRadians,yawRadians,recoverySeconds];
            if (!values.every(Number.isFinite) || amplitude < 0 || amplitude > 0.2 ||
                duration <= 0 || duration > 2 || Math.abs(pitchRadians) > 0.4 ||
                Math.abs(yawRadians) > 0.4 ||
                recoverySeconds <= 0 || recoverySeconds > 4)
                throw new RangeError("Invalid weapon recoil parameters");
            // Viewmodel impulse uses a spring envelope, distinct from the
            // low-amplitude camera kick. This happens *after* ballistics.
            WeaponEffects.#recoilMotion.trigger({
                weaponBackMeters,armImpulse,snapSeconds,
                accentSeconds,settleSeconds,aftershock
            });
            let shake = false, rotation = false;
            try {
                Camera.shake(amplitude * 0.12, duration);
                shake = true;
            } catch(error) {
                Logger.warn("[engine.weaponfx] camera shake: " + String(error));
            }
            try {
                Camera.recoil(pitchRadians,yawRadians,recoverySeconds);
                rotation = true;
            } catch(error) {
                Logger.warn("[engine.weaponfx] camera recoil: " + String(error));
            }
            return Object.freeze({shake,rotation});
        }

        static fire({shot, casingType="pistol", casingPrefab=null,
                      shellImpulse=1, emitCasing=true,
                      casingAudio=null,
                      casingMaterial={friction:.30,restitution:.42},
                      recoil=null}={}) {
            const point=p=>p && [p.x,p.y,p.z].every(Number.isFinite);
            if(!shot || !point(shot.muzzle) ||
               !point(shot.barrelDirection) ||
               !Number.isFinite(shellImpulse) ||
               shellImpulse<0 || shellImpulse>10 ||
               (casingType!=="pistol"&&casingType!=="rifle") ||
               typeof emitCasing!=="boolean")
                throw new TypeError("Invalid weapon effects descriptor");
            const normal=v=>{
                const length=Math.hypot(v.x,v.y,v.z);
                if(length<1e-6)throw new RangeError("Invalid shot vector");
                return {x:v.x/length,y:v.y/length,z:v.z/length};
            };
            if(emitCasing && !shot.casing)
                throw new Error("Every firearm must provide a valid ejectionLocal socket");
            if(recoil!==null) {
                const {amplitude=.020,duration=.13}=recoil;
                if(!Number.isFinite(amplitude)||amplitude<0||amplitude>.2||
                   !Number.isFinite(duration)||duration<=0||duration>2)
                    throw new RangeError("Invalid camera recoil");
            }
            const barrel=normal(shot.barrelDirection);
            // Real native Viewport muzzle effect, independent of the Jolt
            // scene. Missing optional VFX provider must not suppress brass.
            // Failure of one optional visual provider must not suppress
            // camera recoil or the *physical* brass ejection.
            let particles=false;
            try {
                particles=Particles.tryEmit("weapon.muzzle", {
                    position:shot.muzzle, direction:barrel
                });
            } catch(error) {
                Logger.warn("[engine.weaponfx] muzzle VFX: "+String(error));
            }
            // Recoil is independent from smoke, mesh/material loading and
            // cartridge spawning. Never leave it behind a throwing branch.
            if (recoil !== null) WeaponEffects.kick(recoil);
            let casing=null;
            const socket=shot.casing;
            if(emitCasing && socket!==null && socket!==undefined) {
                if(!point(socket.origin) || !point(socket.right) ||
                   !point(socket.up))
                    throw new TypeError("Invalid ejection socket world frame");
                const right=normal(socket.right),up=normal(socket.up);
                // The physically observed ejection port is the source of
                // truth. The gun profile selects pistol/rifle prefab type.
                const prefab=casingPrefab ?? (
                    casingType==="rifle" ?
                        "prefabs/weapon/rifle_shell.nprefab":
                        "prefabs/weapon/pistol_shell.nprefab");
                if(typeof prefab!=="string" ||
                   !/^prefabs\/weapon\/[A-Za-z0-9_-]+\.nprefab$/.test(prefab))
                    throw new TypeError("Unsafe casing prefab reference");
                const variation=(++WeaponEffects.#shots%7-3)*.075;
                const factor=shellImpulse*(1+variation);
                const origin=socket.origin;
                try {
                    const instance=Factory.prefab(prefab,{position:origin});
                    const index=instance.root.index;
                    const vector={
                        x:(right.x*.018+up.x*.012-barrel.x*.003)*factor,
                        y:(right.y*.018+up.y*.012-barrel.y*.003)*factor,
                        z:(right.z*.018+up.z*.012-barrel.z*.003)*factor
                    };
                    // Real physics impulse, not a particle "flying casing".
                    // Crucially: NEVER destroy this instance on FX expiry.
                    const configured=Physics.setBodyMaterial(
                        index,casingMaterial);
                    if(!configured && WeaponEffects.#shots===1)
                        Logger.warn("[engine.weaponfx] Physics provider lacks "+
                            "body surface material ABI; casing bounces use provider defaults");
                    Physics.applyImpulse(index,vector);
                    casing=instance;
                    if(casingAudio) {
                        // Bound the audio registry without deleting physics
                        // actors. Stale records simply age out of sound tracking.
                        if(WeaponEffects.#shellContacts.size>=2048) {
                            const oldest=WeaponEffects.#shellContacts.keys().next().value;
                            WeaponEffects.#shellContacts.delete(oldest);
                        }
                        WeaponEffects.#shellContacts.set(index,{
                            audio:casingAudio,serial:WeaponEffects.#shots,
                            contacts:0,lastContact:-1,rolled:false
                        });
                        WeaponEffects.#ensureContacts();
                    }
                } catch(error) {
                    ++WeaponEffects.#casingFailures;
                    // Failure remains observable, but a missing optional
                    // casing must not abort input, the firing animation, or
                    // the recoil and muzzle flash on subsequent frames.
                    Logger.warn("[engine.weaponfx] physical casing: "+String(error));
                }
            } else ++WeaponEffects.#shots;
            return Object.freeze({particles,casing,
                serial:WeaponEffects.#shots,
                casingFailed:emitCasing && casing===null});
        }

        static stats() {
            return Object.freeze({shots:WeaponEffects.#shots,
                casingFailures:WeaponEffects.#casingFailures,
                trackedShells:WeaponEffects.#shellContacts.size,
                contactSounds:WeaponEffects.#contactSounds,
                rollSounds:WeaponEffects.#rollSounds});
        }
    }
