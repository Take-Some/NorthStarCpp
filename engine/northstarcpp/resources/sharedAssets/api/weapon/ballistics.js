    class WeaponBallistics {
        static solve({weaponIndex, muzzleLocal, ejectionLocal=null,
                      barrelAxisLocal={x:0,y:0,z:1},
                      eye=null, aimDirection=null, mode="auto",
                      range, radius, adsRadius=null,
                      aimRadius=0.006, ignoreIndex=-1}) {
            // Explicit mode/aim makes this usable for NPC and free weapons.
            // Player input is consulted only for the convenience "auto" mode.
            // A possessed first-person gun must hit what the camera is
            // pointing at even while firing from the hip. RMB affects the
            // presentation and precision, not which target is selected.
            // Explicit "barrel" remains available for NPCs/turrets.
            const requestedMode=mode;
            // Explicit AI/turret fire is independent of Player and Camera.
            // Query native view/input only for the user-controlled auto mode.
            const firstPerson=mode==="auto" && Camera.mode()==="first_person";
            const adsActive=firstPerson && Input.mouseDown(1);
            if(mode==="auto")mode=firstPerson?"sight":"barrel";
            if(mode!=="sight" && mode!=="barrel")
                throw new RangeError("Ballistics mode must be auto/sight/barrel");
            // Keep the hip-fire projectile radius separate from the precise
            // ADS radius. Explicit sight-mode callers may opt into ADS.
            if(mode==="sight" && adsRadius!==null &&
               (adsActive || requestedMode==="sight"))
                radius=adsRadius;
            const finitePoint=p=>p && [p.x,p.y,p.z].every(Number.isFinite);
            const unit=p=>{
                const length=Math.hypot(p.x,p.y,p.z);
                if(!Number.isFinite(length) || length<1e-6)
                    throw new RangeError("Ballistics direction must be nonzero");
                return {x:p.x/length,y:p.y/length,z:p.z/length};
            };
            if(!Number.isSafeInteger(weaponIndex) || weaponIndex<0 ||
               !finitePoint(muzzleLocal) || !finitePoint(barrelAxisLocal) ||
               (ejectionLocal!==null && !finitePoint(ejectionLocal)) ||
               (eye!==null && !finitePoint(eye)) ||
               (aimDirection!==null && !finitePoint(aimDirection)) ||
               !Number.isFinite(range) ||
               range<=0 || range>100000 || !Number.isFinite(radius) ||
               radius<=0 || radius>5 || !Number.isFinite(aimRadius) ||
               aimRadius<=0 || aimRadius>5 ||
               !Number.isSafeInteger(ignoreIndex) || ignoreIndex< -1)
                throw new TypeError("Invalid shared weapon ballistic parameters");
            const muzzle=Scene.attachmentPoint(weaponIndex,muzzleLocal);
            if(!finitePoint(muzzle))return null; // no guessed origin
            const axis=unit(barrelAxisLocal);
            const barrelSample=Scene.attachmentPoint(weaponIndex,{
                x:muzzleLocal.x+axis.x*.10,
                y:muzzleLocal.y+axis.y*.10,
                z:muzzleLocal.z+axis.z*.10
            });
            if(!finitePoint(barrelSample))return null;
            const barrel=unit({
                x:barrelSample.x-muzzle.x,
                y:barrelSample.y-muzzle.y,
                z:barrelSample.z-muzzle.z
            });
            // Ejection port and both local basis directions are sampled from
            // the SAME weapon transform as muzzle. They remain correct when
            // the pistol turns, holsters, goes free or switches to ADS.
            const eject=ejectionLocal===null?null:
                Scene.attachmentPoint(weaponIndex,ejectionLocal);
            if(ejectionLocal!==null && !finitePoint(eject))return null;
            const relative=(p,axis)=>Scene.attachmentPoint(weaponIndex,{
                x:p.x+axis.x*.10,y:p.y+axis.y*.10,z:p.z+axis.z*.10
            });
            const ejectX=eject?relative(ejectionLocal,{x:1,y:0,z:0}):null;
            const ejectY=eject?relative(ejectionLocal,{x:0,y:1,z:0}):null;
            if(eject && (!finitePoint(ejectX)||!finitePoint(ejectY)))
                return null;
            const casing=eject?{
                origin:eject,
                right:unit({x:ejectX.x-eject.x,y:ejectX.y-eject.y,
                            z:ejectX.z-eject.z}),
                up:unit({x:ejectY.x-eject.x,y:ejectY.y-eject.y,
                         z:ejectY.z-eject.z})
            }:null;
            if(mode==="sight") {
                // Require an explicit NPC aim or the native possessed camera.
                if(eye===null)eye=Player.eye();
                if(aimDirection===null) {
                    const {yaw,pitch}=Camera.angles();
                    aimDirection={
                        x:Math.sin(yaw)*Math.cos(pitch),
                        y:Math.sin(pitch),
                        z:Math.cos(yaw)*Math.cos(pitch)
                    };
                }
            } else {
                // A free weapon does not need a player camera; its authoritative
                // scene/physics muzzle transform is already available here.
                if(eye===null)eye=muzzle;
                if(aimDirection===null)aimDirection=barrel;
            }
            if(!finitePoint(eye) || !finitePoint(aimDirection))
                throw new TypeError("Invalid eye or aim direction");
            const aim=unit(aimDirection);
            const sightEnd={
                x:eye.x+aim.x*range,y:eye.y+aim.y*range,
                z:eye.z+aim.z*range};
            const cameraHit=mode==="sight"?Collision.sweepSphere(
                eye,sightEnd,aimRadius,ignoreIndex):null;
            const fraction=cameraHit && Number.isFinite(cameraHit.fraction)?
                Math.max(0,Math.min(1,cameraHit.fraction)):1;
            const aimPoint=mode==="sight"?{
                x:eye.x+aim.x*range*fraction,
                y:eye.y+aim.y*range*fraction,
                z:eye.z+aim.z*range*fraction
            }:{
                x:muzzle.x+barrel.x*range,
                y:muzzle.y+barrel.y*range,
                z:muzzle.z+barrel.z*range
            };
            const toward={
                x:aimPoint.x-muzzle.x,
                y:aimPoint.y-muzzle.y,
                z:aimPoint.z-muzzle.z};
            // A camera obstruction immediately behind the muzzle must not
            // cause a backward muzzle ray or originate through a wall.
            const forward=mode==="barrel"?barrel:
                (Math.hypot(toward.x,toward.y,toward.z)>0.001?
                    unit(toward):aim);
            const muzzleDistance=Math.hypot(toward.x,toward.y,toward.z);
            const blocked=mode==="sight" &&
                (muzzleDistance<0.001 ||
                 forward.x*aim.x+forward.y*aim.y+forward.z*aim.z<=0);
            const end=blocked?muzzle:(cameraHit?aimPoint:{
                x:muzzle.x+forward.x*range,
                y:muzzle.y+forward.y*range,
                z:muzzle.z+forward.z*range});
            const hit=blocked?null:Collision.sweepSphere(
                muzzle,end,radius,ignoreIndex);
            return Object.freeze({
                muzzle, aimPoint, direction:forward, barrelDirection:barrel,
                hit, blocked, mode, casing,
                effects:{
                    flash:{origin:muzzle,direction:barrel},
                    smoke:{origin:muzzle,direction:barrel},
                    casing
                },
                cameraRay:{from:eye,to:sightEnd},
                shotRay:{from:muzzle,to:end}
            });
        }
    }

    // Universal weapon-shot service. Each gun supplies a ballistic solution;
    // the engine owns short-lived muzzle VFX and persistent physical brass.
    // No game or weapon prefab contains duplicate ejection logic.
