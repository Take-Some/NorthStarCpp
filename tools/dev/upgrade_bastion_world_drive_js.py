#!/usr/bin/env python3
"""Upgrade project's V8 gameplay script to physical world-coordinate driving.

The world stays fixed. All CityRoot-authored components translate in world
coordinates; Jolt carries the grounded pawn from moving static supports.
Animated tread and wheel poses derive from the actual city displacement.
"""
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[2]
TARGET=ROOT/"tools/templates/moving_cities/main.js"
GAME=ROOT/"Projects/MovingCitiesPrototype/assets/scripts/main.js"
source=TARGET.read_text("utf8")
if source!=GAME.read_text("utf8"):
    raise RuntimeError("Game script diverged from source template; refusing overwrite")
for p in (TARGET,GAME):
    back=p.with_name(p.name+".bak-pre-world-drive-20261117")
    if not back.exists():shutil.copy2(p,back)

source=source.replace(
'// The Rust Sea - seeded land surface and two local-frame CityRoot controllers.\n'
'// Source is a NorthStar project script; physics is delegated to Jolt.\n'
'// CityRoot is kinematic in world coordinates; its local physics frame is fixed.\n'
'// Native city-body contacts, cross-frame actors and wheels are not yet implemented.',
'// Sea of Rust: authored world-coordinate traction cities with animated treads.\n'
'// Bastion moves its actual kinematic colliders; Jolt carries grounded actors.\n'
'// Terrain and boulders have persistent fixed world positions.\n'
'// City suspension and dynamic compound-body contacts remain future work.')
source=source.replace('import { Events, Scene, Player, Input, Keys, UI, Session, Logger } from "@northstar/engine";',
 'import { Events, Scene, Player, Input, Keys, UI, Session, Camera, Logger } from "@northstar/engine";')
start=source.index('const wrap=')
end=source.index('class PursuitPrototype {')
new=r'''// Belt geometry must match tools/dev/bastion_tread_kinematics.py.
const LINK_COUNT=48;
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
function sequenceName(name,prefix){
    const result=new RegExp("^"+prefix+" ([LR]) (\\d{2})$").exec(name);
    return result?{side:result[1]==="L"?-1:1,n:Number(result[2])}:null;
}

class ProceduralPlane {
    #cellCount=0;
    #rockCount=0;
    initialize(objects){
        this.#cellCount=objects.filter(o=>o.name.startsWith("WASTELAND CELL ")).length;
        this.#rockCount=objects.filter(o=>o.name.startsWith("WASTELAND ROCK ")).length;
        if(this.#cellCount!==(RADIUS*2+1)**2)
            throw new Error("Authored world terrain mismatch: "+this.#cellCount);
        // No Scene.moveStatic and no color re-sampling as Bastion travels.
        Logger.info("[RustSea] world-anchored terrain="+this.#cellCount+
            " rocks="+this.#rockCount);
    }
    clear(){this.#cellCount=0;this.#rockCount=0;}
}

class CityRoot {
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
}

class DriveTracks {
    #links=[];
    #wheels=[];
    #lastTravel=NaN;
    initialize(objects){
        this.#links=[];this.#wheels=[];
        for(const item of objects){
            const link=sequenceName(item.name,"BASTION TREAD");
            if(link)this.#links.push({...link,index:item.index,lastAngle:NaN});
            const wheel=sequenceName(item.name,"BASTION ROADWHEEL");
            if(wheel)this.#wheels.push({...wheel,index:item.index});
        }
        if(this.#links.length!==LINK_COUNT*2 ||
           this.#wheels.length!==WHEEL_Z.length*2)
            throw new Error("Bastion track entity set missing: links="+
                this.#links.length+", wheels="+this.#wheels.length);
        this.update(0,0,true);
    }
    update(travel,worldZ,force=false){
        if(!Number.isFinite(travel)||!Number.isFinite(worldZ))return;
        if(!force&&Math.abs(travel-this.#lastTravel)<.0001)return;
        for(const link of this.#links){
            const pose=treadPose(link.n,travel);
            Scene.moveStatic(link.index,{
                x:link.side*TRACK_X,y:pose.y,z:pose.z+worldZ
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
                x,y:2.85,z:WHEEL_Z[wheel.n]+worldZ
            });
            Scene.rotateStatic(wheel.index,{x:angle,y:0,z:0});
        }
        this.#lastTravel=travel;
    }
    clear(){this.#links=[];this.#wheels=[];}
}

class DustTrail {
    #puffs=[];
    #next=0;
    #distance=0;
    #seed=0;
    #step=.0;
    initialize(objects){
        this.#puffs=objects.filter(o=>o.name.startsWith("BASTION DRIVE DUST "))
            .map(o=>({index:o.index,age:99,position:{x:0,y:-20,z:0}}));
        if(this.#puffs.length!==24)throw new Error("Authored drive dust pool missing");
        this.#next=0;this.#distance=0;this.#seed=0;this.#step=0;
    }
    #emit(x,z){
        const p=this.#puffs[this.#next++%this.#puffs.length];
        this.#seed=(Math.imul(this.#seed,1664525)+1013904223)>>>0;
        const noise=(this.#seed/0xffffffff-.5);
        p.age=0;
        p.position={x:x+noise*2.4,y:.02,z:z+noise*2.8};
        Scene.moveStatic(p.index,p.position);
        Scene.rotateStatic(p.index,{x:0,y:(this.#seed%360),z:0});
    }
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

'''
# Explicit replacement: old biome, scene scroll and CityRoot are no longer used.
source=source[:start]+new+source[end:]
source=source.replace('    #terrain=new ProceduralPlane();',
'''    #terrain=new ProceduralPlane();
    #tracks=new DriveTracks();
    #dust=new DustTrail();
    #cameraTimer=0;''')
source=source.replace('''return Math.hypot(p.x-HELM.x,p.z-HELM.z)<4 &&''',
'''return Math.hypot(p.x-HELM.x,p.z-(HELM.z+this.#playerCity.worldZ))<4 &&''')
source=source.replace('''this.#hud.setText("brand","THE RUST SEA / PROCEDURAL WASTELAND");''',
'''this.#hud.setText("brand","THE RUST SEA / WORLD-SPACE CITY DRIVE");''')
source=source.replace('''" m/s  |  TARGET "+ENEMY_CRUISE+" m/s");''',
'''" m/s  |  TARGET "+ENEMY_CRUISE+" m/s");''')
source=source.replace('''        const enemies=objects.filter(o=>o.name.startsWith("RUSTWREN ") ||
            o.name==="CITYROOT Rust Wren")
            .map(o=>({index:o.index,position:{...o.position}}));''',
'''        const enemies=objects.filter(o=>o.name.startsWith("RUSTWREN ") ||
            o.name==="CITYROOT Rust Wren")
            .map(o=>({index:o.index,position:{...o.position}}));
        const cityParts=objects.filter(o=>
            (o.name.startsWith("BASTION ")||o.name==="CITYROOT Bastion") &&
            !o.name.startsWith("BASTION TREAD ") &&
            !o.name.startsWith("BASTION ROADWHEEL ") &&
            !o.name.startsWith("BASTION DRIVE DUST "))
            .map(o=>({index:o.index,position:{...o.position}}));''')
source=source.replace('''        this.#playerCity=new CityRoot("Bastion",[],0);
        this.#enemyCity=new CityRoot("Rust Wren",enemies,START_GAP);
        this.#terrain.initialize(objects);''',
'''        this.#playerCity=new CityRoot("Bastion",cityParts,0);
        this.#enemyCity=new CityRoot("Rust Wren",enemies,START_GAP);
        this.#terrain.initialize(objects);
        this.#tracks.initialize(objects);
        this.#dust.initialize(objects);
        this.#playerCity.renderWorld(true);
        this.#enemyCity.renderWorld(true);''')
source=source.replace('''this.#clock=0;this.#worldTimer=0;this.#keyE=false;this.#keyT=false;''',
'''this.#clock=0;this.#worldTimer=0;this.#cameraTimer=0;
        this.#keyE=false;this.#keyT=false;''')
source=source.replace('''Logger.info("[RustSea] CityRoot target parts: "+enemies.length);''',
'''Logger.info("[RustSea] World-space Bastion="+cityParts.length+
            " parts; Rust Wren="+enemies.length+" parts; no terrain scrolling");''')
source=source.replace('''        const nextPlayer=this.#playerCity.worldZ+this.#speed*dt;''',
'''        const previousZ=this.#playerCity.worldZ;
        const nextPlayer=previousZ+this.#speed*dt;''')
source=source.replace('''        this.#elapsed+=dt;
        this.#worldTimer+=dt;
        if(this.#worldTimer>=0.12){
            this.#worldTimer=0;
            this.#enemyCity.renderRelativeTo(this.#playerCity);
        }
        this.#terrain.update(this.#playerCity.worldZ,dt);''',
'''        const distanceMoved=this.#playerCity.worldZ-previousZ;
        const measuredSpeed=dt>0?distanceMoved/dt:0;
        // PHYSICAL WORLD COORDINATES: ship, target and Jolt deck bodies all move.
        // No ground/rock movement and no camera-space trickery.
        this.#playerCity.renderWorld();
        this.#enemyCity.renderWorld();
        this.#tracks.update(this.#playerCity.worldZ,this.#playerCity.worldZ);
        this.#dust.update(distanceMoved,this.#playerCity.worldZ,measuredSpeed,dt);
        this.#elapsed+=dt;
        this.#cameraTimer+=dt;
        if(this.#cameraTimer>.27&&measuredSpeed>4) {
            this.#cameraTimer=0;
            // Modest mechanical judder, bounded to avoid motion sickness.
            try{if(Camera.available())
                Camera.shake(Math.min(.019,.004+measuredSpeed*.00035),.12);
            }catch(error){Logger.warn("[RustSea] camera shake unavailable: "+String(error));}
        }''')
source=source.replace('''        this.#terrain.clear();
        this.#enemyCity?.clear();''',
'''        this.#terrain.clear();
        this.#tracks.clear();
        this.#dust.clear();
        this.#enemyCity?.clear();''')
required=[
'this.#playerCity.renderWorld();',
'this.#enemyCity.renderWorld();',
'this.#tracks.update(',
'this.#dust.update(',
'BASTION TREAD ',
'WORLD-SPACE CITY DRIVE',
]
if not all(x in source for x in required):raise RuntimeError("Script world drive invariant missing")
if 'renderRelativeTo' in source or 'class ProceduralPlane' not in source:
    raise RuntimeError("Old relative-scrolling logic remains")
for p in (TARGET,GAME):p.write_text(source,"utf8")
print("Updated V8 project and template scripts: world transforms, real tread odometry, dust, camera")
