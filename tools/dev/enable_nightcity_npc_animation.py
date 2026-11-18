#!/usr/bin/env python3
"""Opt-in authored humanoid pose for NPC entities that lack native YCD clips.
Dry-run by default. Idempotent; retains all existing NEF8 records/version IDs.
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
from tools.dev.scene_binary import read_nef8_scene_records, pack_listfile, validate_nef8_scene
from tools.dev.install_nightcity_npcs import (
    SCENE, name_of, version_map, encode_component
)
BACKUP=SCENE.with_suffix(".nscn.before-npc-procedural-20261118.bak")
TYPE="engine.model.procedural_humanoid"
VALUE=b"locomotion_v1"
def update(raw:bytes):
    records=read_nef8_scene_records(raw)
    ids={name_of(b):eid for kind,eid,b in records if kind==2}
    selected=[ids[name] for name in ("NPC Ellie","NPC Isaac")]
    existing={}
    for kind,eid,data in records:
        if kind==7 and name_of(data)==TYPE:
            existing.setdefault(eid,[]).append(data)
    for eid in selected:
        if existing.get(eid) and existing[eid]!=[encode_component(TYPE,1,VALUE)]:
            raise ValueError("Custom NPC animation component exists: "+str(eid))
    extra=[(7,eid,encode_component(TYPE,1,VALUE)) for eid in selected
           if eid not in existing]
    if not extra:return raw,records,selected,False
    updated=records+extra
    blob=pack_listfile(updated,record_versions=version_map(raw))
    if read_nef8_scene_records(blob)!=updated or validate_nef8_scene(blob)!=len(updated):
        raise ValueError("NEF8 animation component integrity failed")
    if updated[:len(records)]!=records:
        raise AssertionError("Original records changed")
    return blob,updated,selected,True
def main():
    a=argparse.ArgumentParser()
    a.add_argument("--apply",action="store_true")
    a.add_argument("--verify",action="store_true")
    v=a.parse_args()
    source=SCENE.read_bytes()
    blob,records,ids,changed=update(source)
    if v.verify:
        if changed:raise ValueError("Two authored NPC procedural components missing")
        print("PASS native NPC animation opt-ins; entities "+str(ids)+"; NEF8 records="+str(len(records)))
        return
    if not v.apply:
        print("DRY RUN add NPC procedural components: "+str(ids)+"; new="+str(changed))
        return
    if not changed:
        print("PASS NPC animation already installed")
        return
    if not BACKUP.exists():BACKUP.write_bytes(source)
    staging=SCENE.with_suffix(".nscn.animation.tmp")
    try:
        staging.write_bytes(blob)
        if read_nef8_scene_records(staging.read_bytes())!=records:
            raise RuntimeError("Staged scene did not round trip")
        if SCENE.read_bytes()!=source:
            raise RuntimeError("Scene changed concurrently: refusing to replace")
        os.replace(staging,SCENE)
    finally:
        if staging.exists():staging.unlink()
    print("APPLIED two engine.model.procedural_humanoid components, backup="+str(BACKUP))
if __name__=="__main__":
    main()
