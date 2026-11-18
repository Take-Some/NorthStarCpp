#!/usr/bin/env python3
"""Refactor Sea of Rust CityRoot to existing NorthStar engine.vehicle provider.

No independent speed integrator, no fake stone scrolling. VehicleService is
authoritative for both cities' world coordinates. CityRoot only synchronizes
compound kinematic parts and dispatches the tracked visual odometer.
"""
from pathlib import Path
import shutil
import ast

ROOT=Path(__file__).resolve().parents[2]
files=[
    ROOT/"tools/templates/moving_cities/main.js",
    ROOT/"Projects/MovingCitiesPrototype/assets/scripts/main.js",
]
a,b=[p.read_text("utf8") for p in files]
if a!=b:raise RuntimeError("Template/project script divergence; manual reconciliation required")
for p in files:
    backup=p.with_name(p.name+".bak-before-vehicle-integration-20261117")
    if not backup.exists():shutil.copy2(p,backup)

src=a
def change(old,new):
    global src
    n=src.count(old)
    if n!=1:raise RuntimeError(f"Expected exactly one match ({n}): {old[:150]!r}")
    src=src.replace(old,new,1)

change(
'import { Events, Scene, Player, Input, Keys, UI, Session, Camera, Logger } from "@northstar/engine";',
'import { Events, Scene, Player, Input, Keys, Vehicle, UI, Session, Camera, Logger } from "@northstar/engine";'
)
change('''class CityRoot {
    #parts=[];
    #worldZ;
    #baseZ;
    #lastZ=NaN;
    constructor(name,parts,worldZ){
        this.name=name;this.#parts=parts;this.#baseZ=worldZ;this.#worldZ=worldZ;
    }
    get worldZ(){return this.#worldZ;}
    move(delta){
        if(!Number.isFinite(delta)||delta<0||delta>2.5)
            throw new RangeError("Invalid continuous CityRoot step");
        this.#worldZ+=delta;
    }
    renderWorld(force=false){
        const dz=this.#worldZ-this.#baseZ;
        if(!force&&Math.abs(dz-this.#lastZ)<.00001)return;
        for(const part of this.#parts){
            Scene.moveStatic(part.index,{
                x:part.position.x,y:part.position.y,
                z:part.position.z+dz
            });
        }
        this.#lastZ=dz;
    }
    clear(){this.#parts=[];}
}''',
'''class CityRoot {
    // VehicleService owns motion. The CityRoot is the entire vehicle's
    // kinematic collection of renderable/physics deck & hull components.
    // The root marker itself moves in the VehicleService host adapter.
    #parts=[];
    #vehicle;
    #base;
    #pose;
    #lastX=NaN;
    #lastZ=NaN;
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
    get speed(){return this.#pose.speed;}
    syncFromVehicle(force=false){
        const current=Vehicle.state(this.#vehicle);
        if(!Number.isFinite(current.x)||!Number.isFinite(current.z)||
           !Number.isFinite(current.speed)||!Number.isFinite(current.heading))
            throw new Error("Vehicle provider returned invalid CityRoot state");
        // This art-pass has unrotated box colliders. Keep straight road-driving
        // until the compound 6DOF/rotation-aware collision-provider path is ready.
        if(Math.abs(current.heading)>.005)
            throw new Error("CityRoot turning requires compound collider rotation");
        this.#pose=current;
        const dx=current.x-this.#base.x,dz=current.z-this.#base.z;
        if(!force&&Math.abs(dx-this.#lastX)<.00001 &&
           Math.abs(dz-this.#lastZ)<.00001)return;
        for(const part of this.#parts){
            Scene.moveStatic(part.index,{
                x:part.position.x+dx,y:part.position.y,
                z:part.position.z+dz
            });
        }
        this.#lastX=dx;this.#lastZ=dz;
    }
    control(throttle,brake){
        return Vehicle.drive(this.#vehicle,{throttle,steer:0,brake});
    }
    route(x,z,speed){
        return Vehicle.navigate(this.#vehicle,{x,z,speed,radius:6});
    }
    clear(){
        try{Vehicle.detach(this.#vehicle);}
        finally{this.#parts=[];}
    }
}''')
change('''        const enemies=objects.filter(o=>o.name.startsWith("RUSTWREN ") ||
            o.name==="CITYROOT Rust Wren")
            .map(o=>({index:o.index,position:{...o.position}}));
        const cityParts=objects.filter(o=>
            (o.name.startsWith("BASTION ")||o.name==="CITYROOT Bastion") &&''',
'''        const enemies=objects.filter(o=>o.name.startsWith("RUSTWREN "))
            .map(o=>({index:o.index,position:{...o.position}}));
        const cityParts=objects.filter(o=>
            o.name.startsWith("BASTION ") &&''')
change('''        this.#playerCity=new CityRoot("Bastion",cityParts,0);
        this.#enemyCity=new CityRoot("Rust Wren",enemies,START_GAP);''',
'''        const bastionRoot=Scene.findByName("CITYROOT Bastion");
        const rustWrenRoot=Scene.findByName("CITYROOT Rust Wren");
        this.#playerCity=new CityRoot("Bastion",bastionRoot,cityParts,
            "vehicles/bastion.nvehicle",28,MAX_SPEED);
        try{
            this.#enemyCity=new CityRoot("Rust Wren",rustWrenRoot,enemies,
                "vehicles/rust_wren.nvehicle",16,ENEMY_CRUISE);
        }catch(error){
            this.#playerCity.clear();this.#playerCity=null;
            throw error;
        }''')
change('''        this.#playerCity.renderWorld(true);
        this.#enemyCity.renderWorld(true);''',
'''        this.#playerCity.syncFromVehicle(true);
        this.#enemyCity.syncFromVehicle(true);''')
change('''                this.#state=1;this.#desired=12;
                this.#announce("Bastion drive engaged. Rust Wren is escaping.");''',
'''                this.#state=1;this.#desired=12;
                this.#enemyCity.route(0,20000,ENEMY_CRUISE);
                this.#announce("Bastion vehicle drive engaged. Rust Wren escaping.");''')
change('''                this.#state=3;this.#desired=0;this.#speed=0;
                this.#announce("Capture complete. City drive stopped.");''',
'''                this.#state=3;this.#desired=0;
                this.#playerCity.control(0,1);
                this.#enemyCity.control(0,1);
                this.#announce("Capture complete. Vehicle brakes applied.");''')
start=src.index('        if(this.#state===0||this.#state===3)return;')
end=src.index('        this.#elapsed+=dt;',start)
src=src[:start]+'''        const previousZ=this.#playerCity.worldZ;
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
            const safeSpeed=Math.sqrt(2*3.7*stopDistance);
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
        this.#tracks.update(this.#playerCity.worldZ,this.#playerCity.worldZ);
        this.#dust.update(distanceMoved,this.#playerCity.worldZ,measuredSpeed,dt);
''' +src[end:]
change('''        Logger.info("[RustSea] World-space Bastion="+cityParts.length+
            " parts; Rust Wren="+enemies.length+" parts; no terrain scrolling");''',
'''        Logger.info("[RustSea] VehicleService CityRoot: Bastion="+cityParts.length+
            " components, Rust Wren="+enemies.length+
            " components; engine.vehicle provider="+Vehicle.provider());''')
change('''        this.#enemyCity?.clear();this.#enemyCity=null;
        this.#playerCity?.clear();this.#playerCity=null;''',
'''        this.#enemyCity?.clear();this.#enemyCity=null;
        this.#playerCity?.clear();this.#playerCity=null;''')
if 'this.#playerCity.move(' in src or 'renderWorld()' in src:
    raise RuntimeError("Legacy free-running CityRoot integrator survived vehicle refactor")
for p in files:p.write_text(src,"utf8")
print("CityRoot now uses engine.vehicle attach/rig/controls/waypoint/state for both cities")
