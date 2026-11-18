"""Replace AbbyPistolDemo's overlapping 5x5 cubes with one static floor slab.

Only the named floor entities are modified. Preserve every remaining stable ID,
parenting link, typed component, authored asset and script reference. Rebuild the
NEF8 v2 envelope with fresh checksums and save a byte-for-byte original backup.
"""
from __future__ import annotations

from pathlib import Path
import shutil
import struct
import sys

ROOT=Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0,str(ROOT))
from tools.create_abby_pistol_demo import entity
from tools.dev.scene_binary import (
    pack_listfile,read_nef8_scene_records,validate_nef8_scene)
from tools.dev.scene_component_length_check import mismatches

SCENE=ROOT/"Projects/AbbyPistolDemo/main.nscn"
BACKUP=SCENE.with_name("main.nscn.before-continuous-floor-20261118.bak")

def get_name(payload:bytes)->str:
    if len(payload)<2:raise ValueError("Invalid entity payload")
    length=struct.unpack_from("<H",payload)[0]
    return payload[2:2+length].decode("utf-8")

def main()->None:
    data=SCENE.read_bytes()
    records=read_nef8_scene_records(data)
    decks=[(eid,blob) for kind,eid,blob in records if kind==2
           and get_name(blob).startswith("Deck ")]
    if not decks:
        floor=[(eid,blob) for kind,eid,blob in records
            if kind==2 and get_name(blob)=="Firing Range Floor"]
        if len(floor)==1:
            print("PASS existing continuous floor already installed")
            return
        raise RuntimeError("Unexpected floor layout; refusing to rewrite")
    if len(decks)!=25 or {eid for eid,_ in decks}!=set(range(2,27)):
        raise RuntimeError("Authored Deck stable IDs changed, refusing to rewrite")
    old_decks={eid for eid,_ in decks}
    components_on_decks=[eid for kind,eid,_ in records if kind==7 and eid in old_decks]
    if components_on_decks:
        raise RuntimeError("Deck holds gameplay components: "+str(components_on_decks))
    floor=entity("Firing Range Floor",2,0,-50,0,50,0x314D62)
    rewritten=[]
    for record in records:
        kind,eid,payload=record
        if kind==2 and eid in old_decks:
            if eid==2: rewritten.append(floor)
        else: rewritten.append(record)
    if len(rewritten)!=len(records)-24:
        raise RuntimeError("Unexpected number of removed Deck entities")
    if len(mismatches(rewritten)):
        raise RuntimeError("Component length mismatch after floor replacement")
    result=pack_listfile(rewritten,record_versions={2:2})
    if validate_nef8_scene(result)!=len(rewritten):
        raise RuntimeError("Invalid rewritten NEF8 scene")
    reread=read_nef8_scene_records(result)
    if reread!=rewritten:
        raise RuntimeError("Reencoded records differ")
    before={eid:(kind,payload) for kind,eid,payload in records if eid not in old_decks}
    after={eid:(kind,payload) for kind,eid,payload in reread if eid not in old_decks}
    if before!=after:
        raise RuntimeError("Unrelated entity/component changed")
    if not BACKUP.exists(): shutil.copy2(SCENE,BACKUP)
    SCENE.write_bytes(result)
    print(f"PASS replaced 25 Deck cubes with one 50x50m floor; "
          f"records={len(records)}->{len(rewritten)}, stable other IDs kept, "
          f"backup={BACKUP.name}")

if __name__=="__main__":main()
