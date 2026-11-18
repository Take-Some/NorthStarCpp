#!/usr/bin/env python3
"""Opt both authored KaylaNightCity NPCs into native *retargeted* YCD idles.
Retains the collision / procedural NPC metadata so yaw remains allowed.
"""
from __future__ import annotations
import argparse,os
from pathlib import Path
from tools.dev.install_nightcity_npcs import SCENE,ROOT,name_of,encode_component,version_map
from tools.dev.scene_binary import read_nef8_scene_records,pack_listfile,validate_nef8_scene
ASSETS=ROOT/"Projects/KaylaNightCity/assets/animations/npcs"
BACKUP=SCENE.with_suffix(".nscn.before-npc-ycd-20261118.bak")
CLIPS={"NPC Ellie":"animations/npcs/ellie_locomotion.ycd@npc-idle",
       "NPC Isaac":"animations/npcs/isaac_locomotion.ycd@npc-idle"}
def transform(raw):
    records=read_nef8_scene_records(raw)
    ids={name_of(body):eid for kind,eid,body in records if kind==2}
    additions=[]
    for name,clip in CLIPS.items():
        eid=ids[name]
        if not (ASSETS/(("ellie" if "Ellie" in name else "isaac")+"_locomotion.ycd")).is_file():
            raise ValueError("Retargeted YCD not generated: "+clip)
        expected=encode_component("engine.model.animation",1,clip.encode("utf8"))
        matches=[body for kind,e,body in records if kind==7 and e==eid and name_of(body)=="engine.model.animation"]
        if len(matches)>1 or matches and matches!=[expected]:
            raise ValueError("Existing authored animation would be overwritten for "+name)
        if not matches:additions.append((7,eid,expected))
    if not additions:return raw,False,ids
    updated=records+additions
    blob=pack_listfile(updated,record_versions=version_map(raw))
    if (read_nef8_scene_records(blob)!=updated or
        validate_nef8_scene(blob)!=len(updated)):raise ValueError("Invalid authored YCD NEF8")
    return blob,True,ids
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--apply",action="store_true")
    parser.add_argument("--verify",action="store_true")
    args=parser.parse_args()
    original=SCENE.read_bytes()
    result,changed,ids=transform(original)
    if args.verify:
        if changed:raise ValueError("Retargeted NPC idle missing in scene")
        print("PASS YCD idle components installed",ids);return
    if not args.apply:
        print("DRY RUN two native idle clips new="+str(changed),ids);return
    if not changed:
        print("PASS native NPC idle already installed");return
    if not BACKUP.exists():BACKUP.write_bytes(original)
    stage=SCENE.with_suffix(".nscn.ycd.tmp")
    try:
        stage.write_bytes(result)
        if SCENE.read_bytes()!=original:raise RuntimeError("Concurrent scene edit, abort")
        os.replace(stage,SCENE)
    finally:
        if stage.exists():stage.unlink()
    print("APPLIED NPC retargeted idle in scene",ids)
if __name__=="__main__":main()
