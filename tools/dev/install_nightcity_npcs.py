#!/usr/bin/env python3
"""Idempotently author Ellie and Isaac as NPC actors in the existing NEF8 Scene V3.

Preserves every original record and the format's existing record versions,
including unrelated vehicles and opaque/plugin records. No imported assets
are changed. Dry-run by default; use --apply and --verify.
"""
from __future__ import annotations
import argparse
import os
import struct
from collections import defaultdict
from pathlib import Path

from tools.dev.scene_binary import _text, _vec, pack_listfile, read_nef8_scene_records, validate_nef8_scene
from tools.dev.scene_binary_v2 import _chunks, _inflate

ROOT=Path(__file__).resolve().parents[2]
SCENE=ROOT/"Projects/KaylaNightCity/main.nscn"
BACKUP=SCENE.with_suffix(".nscn.before-npc-20261118.bak")
CHARACTERS=ROOT/"engine/northstarcpp/resources/sharedAssets/characters"

NPCS=(
    ("NPC Ellie","ellie",(838.5,33.88,2285.0),0x83A5B6),
    ("NPC Isaac","isaac",(843.1,33.88,2284.0),0x78858A),
)
COLLIDER="2 0.649999976 1.79999995 0.649999976 0 0 0 4 2 "+ " ".join(["2"]*16)+' "Custom"'

def version_map(source:bytes):
    chunks,count=_chunks(source)
    # This migration must not merge or rearrange real streaming-cell chunks.
    if len(chunks)!=1 or chunks[0][0]!="":
        raise ValueError("Scene has streaming cell chunks: use a native scene editing path")
    chunk=chunks[0]
    decoded=_inflate(source[chunk[2]:chunk[2]+chunk[3]],chunk[4])
    versions=defaultdict(set)
    for i in range(chunk[1]):
        kind,version, *_=struct.unpack_from("<HHQIIII",decoded,16+i*28)
        versions[kind].add(version)
    if any(len(entries)!=1 for entries in versions.values()):
        raise ValueError("Mixed versions for the same scene record type")
    return {key:next(iter(values)) for key,values in versions.items()}

def name_of(blob:bytes)->str:
    size=struct.unpack_from("<H",blob)[0]
    return blob[2:2+size].decode("utf8")

def encode_actor(name:str,eid:int,asset:str,xyz:tuple[float,float,float],color:int,version:int)->bytes:
    if version not in (1,2):
        raise ValueError("Unsupported authored entity wire version")
    # Models are world-space authored actors, NOT possessed Pawns. Jolt sees a
    # collision-enabled static body that Scene.moveStatic drives kinematically.
    return ( _text(name)+_vec(*xyz)+struct.pack("<fI",0.9,color)
        +(struct.pack("<B",0) if version==1 else b"")
        +struct.pack("<fQQ",0.0,eid,0)
        +_text("NPCs")+_text("")+_text("")+_text(asset)+_text("")
        +_vec(0,0,0)+_vec(1,1,1)
        +struct.pack("<ffBB",0.0,1000000.0,1,1))

def encode_component(name:str, version:int, payload:bytes)->bytes:
    return _text(name)+struct.pack("<II",version,len(payload))+payload

def updated(original:bytes):
    records=read_nef8_scene_records(original)
    versions=version_map(original)
    if versions.get(2) not in (1,2) or versions.get(7) not in (1,2):
        raise ValueError("Unexpected native entity/ECS record version")
    names={name_of(raw): (kind,eid,raw) for kind,eid,raw in records if kind==2}
    # Original preexisting fields must remain unmodified; detect partial or
    # user-customized actors instead of overwriting them.
    existing=sum(name in names for name,_,_,_ in NPCS)
    if existing not in (0,len(NPCS)):
        raise ValueError("Partial NPC placement: inspect before modifying")
    if existing==len(NPCS):
        # User-owned NPCs are never overwritten by this importer.
        for name,kind,xyz,color in NPCS:
            rec=names[name]
            if ("characters/"+kind+"/models/"+kind+".ydd").encode("utf8") not in rec[2]:
                raise ValueError("Existing "+name+" has a different asset; not overwriting")
        return original,records,[names[name][1] for name,_,_,_ in NPCS],False
    used={eid for kind,eid,_ in records if kind==2}
    next_id=max(used,default=0)+1
    # VFS dependencies must be present before authoring their references.
    for _,slug,_,_ in NPCS:
        for relative in ("models/"+slug+".ydd","models/"+slug+".ymt",
                         "materials/"+slug+".ymat","textures/"+slug+".ytd"):
            if not (CHARACTERS/slug/relative).is_file():
                raise FileNotFoundError(str(CHARACTERS/slug/relative))
    layer_names={name_of(raw) for kind,_,raw in records if kind==3}
    after=list(records)
    if "NPCs" not in layer_names:
        after.append((3,0,_text("NPCs")+b"\x01"))
    actor_ids=[]
    for name,slug,xyz,color in NPCS:
        if next_id in used:
            raise ValueError("Entity stable ID collision")
        eid=next_id
        next_id+=1
        actor_ids.append(eid)
        asset="characters/"+slug+"/models/"+slug+".ydd"
        after.append((2,eid,encode_actor(name,eid,asset,xyz,color,versions[2])))
        # Exact capsule dimensions and authored Pawn channel policy are
        # shared with the working player's collider, but NPC is not a Pawn.
        after.append((7,eid,encode_component(
            "northstar.physics.collider",4,COLLIDER.encode("utf8"))))
    encoded=pack_listfile(after,record_versions=versions)
    if validate_nef8_scene(encoded)!=len(after) or read_nef8_scene_records(encoded)!=after:
        raise ValueError("NEF8 canonical integrity/record comparison failed")
    if after[:len(records)]!=records:
        raise AssertionError("Existing project records unexpectedly changed")
    return encoded,after,actor_ids,True

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--apply",action="store_true")
    parser.add_argument("--verify",action="store_true")
    args=parser.parse_args()
    if args.apply and args.verify:
        parser.error("--apply and --verify are mutually exclusive")
    original=SCENE.read_bytes()
    encoded,records,ids,changed=updated(original)
    if args.verify:
        if changed:
            raise ValueError("NPCs not installed in scene")
        print("PASS NPCs present, unmodified NEF8 records valid; IDs="+",".join(map(str,ids)))
        return
    if not args.apply:
        print("DRY RUN: add Ellie and Isaac NPCs, new records="+str(len(records))+
              ", ids="+",".join(map(str,ids))+", update="+str(changed))
        return
    if not changed:
        print("PASS NPCs already present, no rewrite")
        return
    if not BACKUP.exists():
        BACKUP.write_bytes(original)
    tmp=SCENE.with_suffix(".nscn.npc.tmp")
    try:
        tmp.write_bytes(encoded)
        if read_nef8_scene_records(tmp.read_bytes())!=records:
            raise ValueError("Staged scene validation failed")
        # Refuse to overwrite if another editor changed the scene in between.
        if SCENE.read_bytes()!=original:
            raise RuntimeError("Scene changed during NPC install; aborting")
        os.replace(tmp,SCENE)
    finally:
        if tmp.exists():
            tmp.unlink()
    print("APPLIED NPCs Ellie and Isaac, stable IDs="+",".join(map(str,ids))+
          "; original records preserved; backup="+str(BACKUP))
if __name__=="__main__":
    main()
