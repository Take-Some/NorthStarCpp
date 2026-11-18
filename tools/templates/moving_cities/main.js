// Sea of Rust: authored world-coordinate traction cities with animated treads.
// Bastion moves its actual kinematic colliders; Jolt carries grounded actors.
// Terrain and boulders have persistent fixed world positions.
// City suspension and dynamic compound-body contacts remain future work.
import { Factory, Script, Scene, Player, Input, Keys, Vehicle, UI, Session, Camera, Logger } from "@northstar/engine";

const SEED=20261117;
const CELL=28;
const RADIUS=8;
const START_GAP=135;
const TARGET_MIN_GAP=46;
const ENEMY_CRUISE=9;
const MAX_SPEED=34;
const DECK_Y=6.85;
const HELM=Object.freeze({x:0,z:8});
/** @param {number} x @param {number} a @param {number} b */
const clamp=(x,a,b)=>Math.max(a,Math.min(b,x));
// Belt geometry must match tools/dev/bastion_tread_kinematics.py.
const LINK_COUNT=48;
const TRACK_X=23.0;
const WHEEL_RADIUS=1.88;
const WHEEL_Z=[-21,-14,-7,0,7,14,21];
function buildTrackPath(){
    const r=2.4, cy=2.85;
    const positions=[];
    for(let i=0;i<=26;i++)positions.push([cy-r,-22+44*i/26]);
    for(let i=1;i<=14;i++){
        const a=-Math.PI/2+Math.PI*i/14;
        positions.push([cy+r*Math.sin(a),22+r*Math.cos(a)]);
    }
    for(let i=1;i<=26;i++)positions.push([cy+r,22-44*i/26]);
    for(let i=1;i<=14;i++){
        const a=Math.PI/2+Math.PI*i/14;
        positions.push([cy+r*Math.sin(a),-22+r*Math.cos(a)]);
    }
    const lengths=[0];
    for(let i=0;i<positions.length;i++){
        const a=positions[i],b=positions[(i+1)%positions.length];
        lengths.push(lengths[lengths.length-1]+Math.hypot(b[0]-a[0],b[1]-a[1]));
    }
    return {positions,lengths,total:lengths[lengths.length-1]};
}
const TRACK_PATH=buildTrackPath();
/** @param {number} index @param {number} travel */
function treadPose(index,travel){
    const distance=((index*TRACK_PATH.total/LINK_COUNT-travel)%TRACK_PATH.total+
        TRACK_PATH.total)%TRACK_PATH.total;
    const lengths=TRACK_PATH.lengths;
    let lo=0,hi=lengths.length-1;
    while(lo<hi-1){
        const mid=(lo+hi)>>1;
        if(lengths[mid]<=distance)lo=mid;else hi=mid;
    }
    const first=TRACK_PATH.positions[lo];
    const second=TRACK_PATH.positions[(lo+1)%TRACK_PATH.positions.length];
    const f=(distance-lengths[lo])/(lengths[lo+1]-lengths[lo]);
    const y=first[0]+(second[0]-first[0])*f;
    const z=first[1]+(second[1]-first[1])*f;
    const deg=-Math.atan2(second[0]-first[0],second[1]-first[1])*180/Math.PI;
    return {y,z,deg};
}
/** @param {string} name @param {string} prefix */
function sequenceName(name,prefix){
    const result=new RegExp("^"+prefix+" ([LR]) (\\d{2})$").exec(name);
    return result?{side:result[1]==="L"?-1:1,n:Number(result[2])}:null;
}

class ProceduralPlane {
    #cellCount=0;
    #rockCount=0;
    /** @param {import("@northstar/engine").SceneEntity[]} objects */
    initialize(objects){
        this.#cellCount=objects.filter(o=>(o.name??"").startsWith("WASTELAND CELL ")).length;
        this.#rockCount=objects.filter(o=>(o.name??"").startsWith("WASTELAND ROCK ")).length;
        if(this.#cellCount!==0 || this.#rockCount!==0)
            throw new Error("Legacy finite wasteland must be removed; use TerrainProvider chunks");
        // No Scene.moveStatic and no color re-sampling as Bastion travels.
        Logger.info("[RustSea] world-anchored TerrainProvider heightfield, seed=20261117");
    }
    clear(){this.#cellCount=0;this.#rockCount=0;}
}

class CityRoot {
    // VehicleService owns motion. The CityRoot is the entire vehicle's
    // kinematic collection of renderable/physics deck & hull components.
    // The root marker itself moves in the VehicleService host adapter.
    /** @type {{index:number;position:import("@northstar/engine").Vector3}[]} */
    #parts=[];
    #vehicle;
    #base;
    #pose;
    #lastX=NaN;
    #lastY=NaN;
    #lastZ=NaN;
    #headingSafetyTriggered=false;
    /** @param {string} name @param {import("@northstar/engine").SceneEntity} marker
     * @param {{index:number;position:import("@northstar/engine").Vector3}[]} parts
     * @param {string} rig @param {number} wheelbase @param {number} maxSpeed */
    constructor(name,marker,parts,rig,wheelbase,maxSpeed){
        this.name=name;
        this.#parts=parts;
        this.#vehicle=marker.index;
        this.#base={x:marker.position.x,y:marker.position.y,z:marker.position.z};
        if(!Vehicle.available())
            throw new Error("Sea of Rust requires the native engine.vehicle provider");
        Vehicle.attach(this.#vehicle,wheelbase,maxSpeed);
        try{
            Vehicle.configureRig(this.#vehicle,rig);
            this.#pose=Vehicle.state(this.#vehicle);
        }catch(error){
            Vehicle.detach(this.#vehicle);
            throw error;
        }
    }
    get id(){return this.#vehicle;}
    get worldZ(){return this.#pose.z;}
    get worldY(){return this.#pose.y;}
    get speed(){return this.#pose.speed;}
    syncFromVehicle(force=false){
        const current=Vehicle.state(this.#vehicle);
        if(!Number.isFinite(current.x)||!Number.isFinite(current.y)||
            !Number.isFinite(current.z)||
           !Number.isFinite(current.speed)||!Number.isFinite(current.heading))
            throw new Error("Vehicle provider returned invalid CityRoot state");
        // Do not kill the script when the vehicle naturally develops small
        // yaw under load: that previously stopped synchronization while the
        // native vehicle continued driving out of the camera's view.
        // This prototype's colliders cannot yet rotate as one compound body.
        // Brake only on material yaw; retain world translation and player.
        if(Math.abs(current.heading)>.08 && !this.#headingSafetyTriggered){
            this.#headingSafetyTriggered=true;
            Vehicle.drive(this.#vehicle,{throttle:0,steer:0,brake:1});
            Logger.warn("[RustSea] city yaw exceeded prototype compound "+
                "collision tolerance; vehicle brake applied.");
        }
        this.#pose=current;
        const dx=current.x-this.#base.x,dy=current.y-this.#base.y,
              dz=current.z-this.#base.z;
        if(!force&&Math.abs(dx-this.#lastX)<.00001 &&
           Math.abs(dy-this.#lastY)<.00001 &&
           Math.abs(dz-this.#lastZ)<.00001)return;
        for(const part of this.#parts){
            Scene.moveStatic(part.index,{
                x:part.position.x+dx,y:part.position.y+dy,
                z:part.position.z+dz
            });
        }
        this.#lastX=dx;this.#lastY=dy;this.#lastZ=dz;
    }
    /** @param {number} throttle @param {number} brake */
    control(throttle,brake){
        // An unsupported compound-body yaw must hold the emergency brake,
        // even when normal throttle code runs later in the same frame.
        return Vehicle.drive(this.#vehicle,this.#headingSafetyTriggered?
            {throttle:0,steer:0,brake:1}:{throttle,steer:0,brake});
    }
    /** @param {number} x @param {number} z @param {number} speed */
    route(x,z,speed){
        return Vehicle.navigate(this.#vehicle,{x,z,speed,radius:6});
    }
    clear(){
        try{Vehicle.detach(this.#vehicle);}
        finally{this.#parts=[];}
    }
}

class DriveTracks {
    /** @type {{side:number;n:number;index:number;lastAngle:number}[]} */
    #links=[];
    /** @type {{side:number;n:number;index:number}[]} */
    #wheels=[];
    #lastTravel=NaN;
    #lastWorldY=NaN;
    /** @param {import("@northstar/engine").SceneEntity[]} objects */
    initialize(objects){
        this.#links=[];this.#wheels=[];
        for(const item of objects){
            const link=sequenceName(item.name??"","BASTION TREAD");
            if(link)this.#links.push({...link,index:item.index,lastAngle:NaN});
            const wheel=sequenceName(item.name??"","BASTION ROADWHEEL");
            if(wheel)this.#wheels.push({...wheel,index:item.index});
        }
        if(this.#links.length!==LINK_COUNT*2 ||
           this.#wheels.length!==WHEEL_Z.length*2)
            throw new Error("Bastion track entity set missing: links="+
                this.#links.length+", wheels="+this.#wheels.length);
        this.update(0,0,0,true);
    }
    /** @param {number} travel @param {number} worldZ @param {boolean} [force] */
    update(travel,worldZ,worldY=0,force=false){
        if(!Number.isFinite(travel)||!Number.isFinite(worldZ))return;
        if(!force&&Math.abs(travel-this.#lastTravel)<.0001 &&
           Math.abs(worldY-this.#lastWorldY)<.0001)return;
        for(const link of this.#links){
            const pose=treadPose(link.n,travel);
            Scene.moveStatic(link.index,{
                x:link.side*TRACK_X,y:pose.y+worldY,z:pose.z+worldZ
            });
            if(force||Math.abs(pose.deg-link.lastAngle)>.08){
                Scene.rotateStatic(link.index,{x:pose.deg,y:0,z:0});
                link.lastAngle=pose.deg;
            }
        }
        const angle=(travel/WHEEL_RADIUS*180/Math.PI)%360;
        for(const wheel of this.#wheels){
            const x=wheel.side*26.1;
            Scene.moveStatic(wheel.index,{
                x,y:2.85+worldY,z:WHEEL_Z[wheel.n]+worldZ
            });
            Scene.rotateStatic(wheel.index,{x:angle,y:0,z:0});
        }
        this.#lastTravel=travel;this.#lastWorldY=worldY;
    }
    clear(){this.#links=[];this.#wheels=[];this.#lastTravel=NaN;this.#lastWorldY=NaN;}
}

class DustTrail {
    /** @type {{index:number;age:number;position:import("@northstar/engine").Vector3}[]} */
    #puffs=[];
    #next=0;
    #distance=0;
    #seed=0;
    #step=.0;
    /** @param {import("@northstar/engine").SceneEntity[]} objects */
    initialize(objects){
        this.#puffs=objects.filter(o=>(o.name??"").startsWith("BASTION DRIVE DUST "))
            .map(o=>({index:o.index,age:99,position:{x:0,y:-20,z:0}}));
        if(this.#puffs.length!==24)throw new Error("Authored drive dust pool missing");
        this.#next=0;this.#distance=0;this.#seed=0;this.#step=0;
    }
    /** @param {number} x @param {number} z */
    #emit(x,z){
        const p=this.#puffs[this.#next++%this.#puffs.length];
        this.#seed=(Math.imul(this.#seed,1664525)+1013904223)>>>0;
        const noise=(this.#seed/0xffffffff-.5);
        p.age=0;
        p.position={x:x+noise*2.4,y:.02,z:z+noise*2.8};
        Scene.moveStatic(p.index,p.position);
        Scene.rotateStatic(p.index,{x:0,y:(this.#seed%360),z:0});
    }
    /** @param {number} delta @param {number} worldZ @param {number} speed @param {number} dt */
    update(delta,worldZ,speed,dt){
        for(const p of this.#puffs){
            if(p.age>1.55)continue;
            p.age+=dt;
            if(p.age>1.55){
                Scene.moveStatic(p.index,{x:p.position.x,y:-20,z:p.position.z});
            }else{
                Scene.moveStatic(p.index,{
                    x:p.position.x+(p.age*.30),
                    y:p.position.y+p.age*.38,
                    z:p.position.z-p.age*.55
                });
            }
        }
        if(speed<2.5||delta<=0)return;
        this.#distance+=delta;
        let iterations=0;
        while(this.#distance>=2.4&&iterations<6){
            this.#distance-=2.4;
            this.#emit(-23.4,worldZ-17.);
            this.#emit(23.4,worldZ-17.);
            iterations++;
        }
    }
    clear(){this.#puffs=[];}
}

class PursuitPrototype extends Script {
    /** @type {CityRoot|null} */
    #playerCity=null;
    /** @type {CityRoot|null} */
    #enemyCity=null;
    #terrain=new ProceduralPlane();
    #tracks=new DriveTracks();
    #dust=new DustTrail();
    #cameraTimer=0;
    /** @type {import("@northstar/engine").UIRoot|null} */
    #hud=null;
    #state=0;
    #speed=0;
    #desired=0;
    #elapsed=0;
    #clock=0;
    #keyE=false;
    #keyT=false;
    #worldTimer=0;
    #message="";

    get distance(){
        if(!this.#enemyCity||!this.#playerCity)
            throw new Error("CityRoot is not initialized");
        return this.#enemyCity.worldZ-this.#playerCity.worldZ;
    }

    #playerNearHelm(){
        const pawn=Scene.findPawn();
        if(!pawn||!this.#playerCity)return false;
        const p=pawn.position;
        return Math.hypot(p.x-HELM.x,p.z-(HELM.z+this.#playerCity.worldZ))<4 &&
               Math.abs(p.y-DECK_Y)<7;
    }
    /** @param {string} message */
    #announce(message){
        this.#message=message;
        Logger.info("[RustSea] "+message);
        this.#renderHUD();
    }
    #renderHUD(){
        if(!this.#playerCity||!this.#enemyCity)return;
        if(!this.#hud)this.#hud=UI.loadAsset("ui/chase_hud.nui",{layer:"mission"});
        const hud=this.#hud;
        const city=this.#playerCity;
        if(!hud||!city)return;
        const stages=["BOARD THE BRIDGE","CITY PURSUIT","INTERCEPT","CAPTURE COMPLETE"];
        const objectives=[
            "Bastion is moving. I accelerate / K brake. Reach the helm.",
            "I accelerate / K brake. Chase the Rust Wren over the wasteland.",
            "Return to the helm and press E to secure the target.",
            "Target secured. T resets the mission."
        ];
        const distance=this.distance;
        hud.batch(()=>{
            hud.setText("brand","THE RUST SEA / WORLD-SPACE CITY DRIVE");
            hud.setText("stage",stages[this.#state]);
            hud.setText("objective",objectives[this.#state]);
            hud.setText("progress","BASTION "+this.#speed.toFixed(1)+
                " m/s  |  TARGET "+ENEMY_CRUISE+" m/s");
            hud.setText("timer","GAP "+distance.toFixed(1)+" m  |  TRAVEL "+
                city.worldZ.toFixed(0)+" m  |  "+this.#elapsed.toFixed(0)+" s");
            hud.setText("controls",
                this.#state===3?"T RESTART  F10 EXIT":
                "WASD WALK  I/K THROTTLE  E CAPTURE  V CAMERA  T RESET");
        });
        hud.mount();
        UI.setTitle("THE RUST SEA | "+stages[this.#state]+" | "+
                    Math.round(distance)+" m");
    }
    start(){
        if(!Player.enabled || Player.index<0 || !Scene.findPawn())
            throw new Error("A possessed NorthStar Pawn is required.");
        const objects=[...Scene.iterate()];
        // Resolve scene-authored landmarks once, not by repeated O(N) scans.
        const authored=new Map(objects.map(object=>[object.name,object]));
        const enemies=objects.filter(o=>(o.name??"").startsWith("RUSTWREN "))
            .map(o=>({index:o.index,position:{...o.position}}));
        const cityParts=objects.filter(o=>
            (o.name??"").startsWith("BASTION ") &&
            !(o.name??"").startsWith("BASTION TREAD ") &&
            !(o.name??"").startsWith("BASTION ROADWHEEL ") &&
            !(o.name??"").startsWith("BASTION DRIVE DUST "))
            .map(o=>({index:o.index,position:{...o.position}}));
        if(enemies.length<30 || !authored.get("BASTION Helm Console"))
            throw new Error("CityRoot scene geometry missing.");
        const bastionRoot=authored.get("CITYROOT Bastion");
        const rustWrenRoot=authored.get("CITYROOT Rust Wren");
        if(!bastionRoot || !rustWrenRoot)
            throw new Error("Authored CityRoot markers missing.");
        this.#playerCity=new CityRoot("Bastion",bastionRoot,cityParts,
            "vehicles/bastion.nvehicle",28,MAX_SPEED);
        try{
            this.#enemyCity=new CityRoot("Rust Wren",rustWrenRoot,enemies,
                "vehicles/rust_wren.nvehicle",16,ENEMY_CRUISE);
        }catch(error){
            this.#playerCity.clear();this.#playerCity=null;
            throw error;
        }
        this.#terrain.initialize(objects);
        this.#tracks.initialize(objects);
        this.#dust.initialize(objects);
        this.#playerCity.syncFromVehicle(true);
        this.#enemyCity.syncFromVehicle(true);
        // Cities are autonomous physical vehicles: start as soon as the
        // V8 scene initializes, without requiring the Pawn to find the helm.
        this.#state=1;this.#speed=0;this.#desired=12;this.#elapsed=0;
        this.#enemyCity.route(0,20000,ENEMY_CRUISE);
        this.#clock=0;this.#worldTimer=0;this.#cameraTimer=0;
        this.#keyE=false;this.#keyT=false;
        this.#announce("Bastion engines engaged. Rust Wren is moving.");
        Logger.info("[RustSea] VehicleService CityRoot: Bastion="+cityParts.length+
            " components, Rust Wren="+enemies.length+
            " components; engine.vehicle provider="+Vehicle.provider());
    }
    /** @param {number} rawDt */
    update(rawDt){
        if(Player.paused||!this.#playerCity||!this.#enemyCity)return;
        if(!Number.isFinite(rawDt)||rawDt<0)return;
        // Never skip CityRoot synchronization during a long render hitch.
        // The vehicle service may have advanced while rendering was delayed.
        const dt=clamp(rawDt,0,0.05);
        const t=Input.isDown(Keys.T);
        if(t&&!this.#keyT){Session.restart();return;}
        this.#keyT=t;
        const e=Input.isDown(Keys.E);
        if(e&&!this.#keyE&&this.#playerNearHelm()){
            if(this.#state===0){
                this.#state=1;this.#desired=12;
                this.#enemyCity.route(0,20000,ENEMY_CRUISE);
                this.#announce("Bastion vehicle drive engaged. Rust Wren escaping.");
            }else if(this.#state===2){
                this.#state=3;this.#desired=0;
                this.#playerCity.control(0,1);
                this.#enemyCity.control(0,1);
                this.#announce("Capture complete. Vehicle brakes applied.");
            }
        }
        this.#keyE=e;
        const previousZ=this.#playerCity.worldZ;
        // VehicleService is integrated in the host after the script tick:
        // authoritative state is read from the preceding vehicle step.
        this.#playerCity.syncFromVehicle();
        this.#enemyCity.syncFromVehicle();
        this.#speed=Math.max(0,this.#playerCity.speed);
        if(this.#state===1||this.#state===2){
            const throttle=Input.isDown(Keys.I);
            const braking=Input.isDown(Keys.K);
            if(throttle&&!braking)
                this.#desired=clamp(this.#desired+11*dt,0,MAX_SPEED);
            if(braking&&!throttle)
                this.#desired=clamp(this.#desired-14*dt,0,MAX_SPEED);
            // Collision-safe approach speed. The real engine.vehicle solver
            // executes acceleration, traction, drag and braking itself.
            const gap=this.distance;
            const stopDistance=Math.max(0,gap-TARGET_MIN_GAP);
            // Relative closing speed is measured against the MOVING target.
            // The old world-zero formula stalled just outside intercept range.
            const leadSpeed=Math.max(0,this.#enemyCity.speed);
            const safeSpeed=leadSpeed+Math.sqrt(2*3.7*stopDistance);
            const requested=Math.min(this.#desired,safeSpeed);
            const error=requested-this.#speed;
            this.#playerCity.control(clamp(error*.32,0,1),
                clamp(-error*.42,0,1));
        }else if(this.#state===3){
            this.#playerCity.control(0,1);
        }
        const distanceMoved=this.#playerCity.worldZ-previousZ;
        const measuredSpeed=dt>0?distanceMoved/dt:0;
        // Attached world parts use engine.vehicle states. Terrain stays fixed.
        this.#tracks.update(this.#playerCity.worldZ,this.#playerCity.worldZ,
            this.#playerCity.worldY);
        this.#dust.update(distanceMoved,this.#playerCity.worldZ,measuredSpeed,dt);
        this.#elapsed+=dt;
        // Passenger camera is stabilized in the ship's moving frame.
        // Do NOT inject periodic mechanical camera shake while aboard.
        // The engine, tracks, dust and sound communicate speed instead.
        this.#cameraTimer=0;
        if(this.distance<=TARGET_MIN_GAP+6&&this.#state===1){
            this.#state=2;
            this.#announce("In tow range. Return to the helm and press E.");
        }
        this.#clock+=dt;
        if(this.#clock>=0.20){this.#clock=0;this.#renderHUD();}
    }
    stop(){
        this.#hud?.dispose();this.#hud=null;
        this.#terrain.clear();
        this.#tracks.clear();
        this.#dust.clear();
        this.#enemyCity?.clear();this.#enemyCity=null;
        this.#playerCity?.clear();this.#playerCity=null;
        Logger.info("[RustSea] stopped");
    }
    onDestroy(){
        // Partial start failures must not strand native vehicle bindings.
        if(this.#playerCity||this.#enemyCity||this.#hud)this.stop();
    }
}
Factory.script(PursuitPrototype);
