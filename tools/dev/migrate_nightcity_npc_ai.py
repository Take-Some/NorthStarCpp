#!/usr/bin/env python3
"""Migrate Ellie & Isaac from kinematic Scene teleports to NavMesh/Jolt AI Pawn.
Dry-run by default. Transaction checks original source hashes before replacing,
retains stable IDs and every unrelated NEF8 record, and creates backups.
"""
from __future__ import annotations
import argparse
import datetime
import os
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from tools.dev.install_nightcity_npcs import (
    SCENE,ROOT,name_of,encode_component,version_map
)
from tools.dev.scene_binary import (
    pack_listfile,read_nef8_scene_records,validate_nef8_scene
)

SCRIPT=ROOT/"Projects/KaylaNightCity/assets/scripts/main.js"
NPCS={"NPC Ellie":1.20,"NPC Isaac":1.05}
PAWN="northstar.gameplay.pawn"
MOTOR="northstar.gameplay.character_motor"

def patched_scene(original:bytes):
    records=read_nef8_scene_records(original)
    version=version_map(original)
    if version.get(2) not in (1,2) or version.get(7) not in (1,2):
        raise ValueError("Unsupported Scene object/component wire versions")
    by_name={name_of(body):(eid,body) for k,eid,body in records if k==2}
    if not all(name in by_name for name in NPCS):
        raise ValueError("Both scene NPCs are required")
    ids={by_name[name][0] for name in NPCS}
    for name in NPCS:
        eid,record=by_name[name]
        if len(record)<80 or not 0<eid<2**64:
            raise ValueError("Invalid native NPC actor record")
        if b"characters/"+(b"ellie" if "Ellie" in name else b"isaac") not in record:
            raise ValueError("NPC visual mesh unexpectedly changed")
    current={}
    for kind,eid,body in records:
        if kind!=7 or eid not in ids:continue
        current[(eid,name_of(body))]=body
    for name in NPCS:
        eid=by_name[name][0]
        for component in (PAWN,MOTOR,"northstar.physics.collider",
                          "engine.model.animation"):
            if component in (PAWN,MOTOR):continue
            if (eid,component) not in current:
                raise ValueError(f"{name}: missing required native {component}")

    additions=[]
    changed=False
    output=[]
    for kind,eid,raw in records:
        if kind!=2 or eid not in ids:
            output.append((kind,eid,raw))
            continue
        length=struct.unpack_from("<H",raw,0)[0]
        offset=2+length+12+4+4+(1 if version[2]==1 else 0)
        if offset+4>len(raw):
            raise ValueError("Truncated actor mass field")
        current_mass=struct.unpack_from("<f",raw,offset)[0]
        if current_mass not in (0.,72.):
            raise ValueError(f"Refusing to overwrite customized NPC mass {current_mass}")
        if current_mass==0.:
            payload=bytearray(raw)
            struct.pack_into("<f",payload,offset,72.)
            raw=bytes(payload)
            changed=True
        output.append((kind,eid,raw))
    for name,speed in NPCS.items():
        eid=by_name[name][0]
        pawn_payload=struct.pack("<III",1,0,0) # humanoid; never auto-possess
        motor_payload=struct.pack("<6f",speed,speed*2.0,3.5,18.,.35,1.)
        for component,payload in ((PAWN,pawn_payload),(MOTOR,motor_payload)):
            old=current.get((eid,component))
            expected=encode_component(component,1,payload)
            if old is not None and old!=expected:
                raise ValueError(f"Refusing to overwrite customized component {component}")
            if old is None:
                additions.append((7,eid,expected))
                changed=True
    if not changed:
        return original,False
    output.extend(additions)
    updated=pack_listfile(output,record_versions=version)
    if validate_nef8_scene(updated)!=len(output):
        raise ValueError("Repacked Scene failed NEF8 validation")
    if read_nef8_scene_records(updated)!=output:
        raise ValueError("Scene NEF8 component/ID roundtrip failed")
    # All records except two masses and appended Pawn/Motor components intact.
    same=[r for r in output if r[0]!=2 or r[1] not in ids]
    original_same=[r for r in records if r[0]!=2 or r[1] not in ids]
    if same[:len(original_same)]!=original_same:
        raise AssertionError("Scene migration altered unrelated record")
    return updated,True

REPLACEMENT=r'''    #stopNavigation(agent) {
        if(agent.activeTarget===null) return;
        try { engine.ai.navigation.stop(String(agent.actor.id)); }
        catch(error) {
            if(!this.#warningIssued) {
                Logger.warn("[KaylaNightCity] AI stop: "+String(error));
                this.#warningIssued=true;
            }
        }
        agent.activeTarget=null;
    }

    #advance(agent,dt,player) {
        if (!agent.actor.alive) return;
        const p=agent.actor.position;
        if (!p) return;
        if(agent.lastPhysicalPosition) {
            const walked=Math.hypot(p.x-agent.lastPhysicalPosition.x,
                                    p.z-agent.lastPhysicalPosition.z);
            if(walked>.02)agent.moves++;
        }
        agent.lastPhysicalPosition={x:p.x,y:p.y,z:p.z};
        if (this.#talk?.id===agent.actor.id) {
            this.#stopNavigation(agent);
            this.#state(agent,"talk");
            return;
        }
        const playerDistance=player?
            Math.hypot(player.x-p.x,player.z-p.z):Infinity;
        if (playerDistance<(agent.role==="guard"?5.0:3.8)) {
            this.#stopNavigation(agent);
            this.#state(agent,"observe");
            return;
        }
        if (agent.idleSeconds>0) {
            this.#stopNavigation(agent);
            agent.idleSeconds=Math.max(0,agent.idleSeconds-dt);
            this.#state(agent,"idle");
            return;
        }
        const wp=agent.route[agent.waypoint];
        const target={x:agent.origin.x+wp.x,y:agent.origin.y,
                      z:agent.origin.z+wp.z};
        const distance=Math.hypot(target.x-p.x,target.z-p.z);
        if (distance<0.65) {
            this.#stopNavigation(agent);
            agent.waypoint=(agent.waypoint+1)%agent.route.length;
            agent.idleSeconds=0.8+(agent.waypoint%3)*0.35;
            this.#state(agent,"idle");
            agent.blockedSeconds=0;
            return;
        }
        if(agent.activeTarget!==agent.waypoint) {
            try {
                // The engine owns NavMesh routing and Jolt's physical motor.
                // No setPosition / kinematic movement for game NPCs.
                engine.ai.navigation.moveTo(String(agent.actor.id),
                    target.x,target.y,target.z);
                agent.activeTarget=agent.waypoint;
                agent.lastProgress=distance;
                agent.blockedSeconds=0;
            } catch(error) {
                this.#state(agent,"blocked");
                if(!this.#warningIssued) {
                    Logger.warn("[KaylaNightCity] NavMesh NPC: "+String(error));
                    this.#warningIssued=true;
                }
                return;
            }
        }
        if(distance<agent.lastProgress-.12) {
            agent.lastProgress=distance;
            agent.blockedSeconds=0;
        } else agent.blockedSeconds+=dt;
        if(agent.blockedSeconds>5) {
            // A blocked route yields, rather than teleporting across a wall.
            this.#stopNavigation(agent);
            agent.blockedSeconds=0;
            agent.idleSeconds=.55;
            agent.waypoint=(agent.waypoint+1)%agent.route.length;
            this.#state(agent,"avoid");
            return;
        }
        this.#state(agent,"walk");
    }

'''
def patched_script(text:str):
    if "engine.ai.navigation.moveTo(String(agent.actor.id)" in text:
        if "agent.actor.setPosition(chosen.to)" in text:
            raise ValueError("Mixed legacy and NavMesh NPC movement")
        return text,False
    start=text.find("    #advance(agent,dt,player) {")
    stop=text.find("    #show(name,message) {",start)
    if start<0 or stop<=start:
        raise ValueError("Existing NPC director movement signature changed")
    if "agent.actor.setPosition(chosen.to)" not in text[start:stop]:
        raise ValueError("Refusing to replace unknown NPC movement implementation")
    updated=text[:start]+REPLACEMENT+text[stop:]
    expected="                blockedSeconds:0, cooldown:1.5, idleSeconds:0.8,"
    if updated.count(expected)!=1:
        raise ValueError("NPC state declaration changed")
    updated=updated.replace(expected,
        "                activeTarget:null, lastProgress:Infinity,\n"+
        expected,1)
    if updated.count("            this.#groundAtSpawn(agent);")!=1:
        raise ValueError("NPC initial grounding source has changed")
    updated=updated.replace("            this.#groundAtSpawn(agent);",
                            "            // Physics provider settles the spawned Pawn.",1)
    # The old kinematic stop never ended native AI motor intent.
    updated=updated.replace(
        "        this.#actors=[];\n",
        "        for(const agent of this.#actors) this.#stopNavigation(agent);\n"+
        "        this.#actors=[];\n",1)
    return updated,True

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--apply",action="store_true")
    parser.add_argument("--verify",action="store_true")
    args=parser.parse_args()
    original_scene=SCENE.read_bytes()
    original_script=SCRIPT.read_bytes()
    updated_scene,scene_changed=patched_scene(original_scene)
    updated_script,script_changed=patched_script(original_script.decode("utf8"))
    if args.verify:
        if scene_changed or script_changed:
            raise ValueError("Native NPC Pawn/NavMesh migration is not installed")
        print("PASS native NPCs are Pawn, project script uses NavMesh commands")
        return
    print("NPC Pawn migration:", "scene_changed="+str(scene_changed),
          "script_changed="+str(script_changed))
    if not args.apply or not (scene_changed or script_changed):return
    backup=SCENE.parent/"migration"/(
        "npc_ai_before_"+datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    backup.mkdir(parents=True,exist_ok=False)
    (backup/"main.nscn").write_bytes(original_scene)
    (backup/"main.js").write_bytes(original_script)
    stage_scene=SCENE.with_suffix(".nscn.ai-migration.tmp")
    stage_script=SCRIPT.with_suffix(".js.ai-migration.tmp")
    try:
        stage_scene.write_bytes(updated_scene)
        stage_script.write_text(updated_script,"utf8")
        if SCENE.read_bytes()!=original_scene or SCRIPT.read_bytes()!=original_script:
            raise RuntimeError("Concurrent editor/source changes detected")
        os.replace(stage_scene,SCENE)
        os.replace(stage_script,SCRIPT)
    finally:
        stage_scene.unlink(missing_ok=True)
        stage_script.unlink(missing_ok=True)
    print("APPLIED NPC Pawns / NavMesh project script; backups:",backup)

if __name__=="__main__":
    main()
