#!/usr/bin/env python3
"""Calibrate live NorthStar humanoid CharacterMotor scenes using Abby YCD
stance-phase measurements. Preserves jump/acceleration/air/gravity and all
other scene records. Never touches migrations, backups, or other asset types.

Usage:
  python tools/analysis/apply_abby_locomotion_speeds.py       # dry-run
  python tools/analysis/apply_abby_locomotion_speeds.py --apply
  python tools/analysis/apply_abby_locomotion_speeds.py --verify
"""
from __future__ import annotations
import argparse
from collections import defaultdict
import os
from pathlib import Path
import struct
import sys
ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0,str(ROOT))

from tools.dev.scene_binary import (
    pack_listfile, read_nef8_scene_records, validate_nef8_scene
)
from tools.dev.scene_binary_v2 import _chunks, _inflate

ROOT = Path(__file__).resolve().parents[2]
PROJECTS = [
    "Projects/AbbyPistolDemo/main.nscn",
    "Projects/NorthStarFirstPersonPlatform/main.nscn",
    "Projects/KaylaNightCity/main.nscn",
    "Projects/MovingCitiesPrototype/main.nscn",
]
OLD = (5., 8.)
NEW = (2.95, 4.44) # WASD run, Shift sprint; Ctrl scales to 1.48 m/s.
TAG = "northstar.gameplay.character_motor"

def record_versions(source:bytes):
    cells, _ = _chunks(source)
    if len(cells)!=1 or cells[0][0] != "":
        raise ValueError("Nontrivial streaming-cell layout; refusing to repack")
    c = cells[0]
    raw = _inflate(source[c[2]:c[2]+c[3]], c[4])
    versions=defaultdict(set)
    for i in range(c[1]):
        kind, version, *_ = struct.unpack_from("<HHQIIII",raw,16+i*28)
        versions[kind].add(version)
    if any(len(v)!=1 for v in versions.values()):
        raise ValueError("Multiple record versions for a kind; preserve manually")
    return {k:next(iter(v)) for k,v in versions.items()}

def convert(original:bytes, target:tuple[float,float]):
    before=read_nef8_scene_records(original)
    after=[]
    found=0
    for kind,eid,raw in before:
        payload=raw
        if kind==7 and len(raw)>14:
            n=struct.unpack_from("<H",raw)[0]
            label=raw[2:2+n].decode("utf8")
            if label==TAG:
                offset=2+n+8
                version,length=struct.unpack_from("<II",raw,2+n)
                if version!=1 or length!=24 or len(raw)!=offset+24:
                    raise ValueError(f"CharacterMotor v1 24-byte contract changed: {eid}")
                current=struct.unpack_from("<6f",raw,offset)
                if any(abs(current[i]-expected)>.00001
                       for i,expected in enumerate(OLD)):
                    if all(abs(current[i]-target[i])<.00001 for i in range(2)):
                        pass # --verify and idempotence
                    else:
                        raise ValueError(f"Unexpected authored speeds: {current[:2]}")
                payload=raw[:offset]+struct.pack("<2f",*target)+raw[offset+8:]
                found+=1
        after.append((kind,eid,payload))
    if found!=1:
        raise ValueError(f"Expected exactly one CharacterMotor, found {found}")
    rebuilt=pack_listfile(after,record_versions=record_versions(original))
    if validate_nef8_scene(rebuilt)!=len(before):
        raise ValueError("Packed scene failed NEF8 validation")
    decoded=read_nef8_scene_records(rebuilt)
    if decoded!=after or len(decoded)!=len(before):
        raise ValueError("Component data or ordering changed")
    return rebuilt

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--apply",action="store_true")
    parser.add_argument("--verify",action="store_true")
    args=parser.parse_args()
    for relative in PROJECTS:
        target=ROOT/relative
        original=target.read_bytes()
        if args.verify:
            candidate=convert(original,NEW)
            if candidate==original:
                print(f"PASS {relative}: motor calibrated and all records valid")
                continue
            # Canonical compression can differ without changing record content.
            if read_nef8_scene_records(candidate)==read_nef8_scene_records(original):
                print(f"PASS {relative}: motor calibrated and all records valid")
                continue
            raise ValueError("Scene motor still not calibrated: "+relative)
        candidate=convert(original,NEW)
        if not args.apply:
            print(f"DRY RUN {relative}: 5/8 -> {NEW}; records validated")
            continue
        backup=target.with_suffix(target.suffix+".before-abby-gait.bak")
        if not backup.exists():backup.write_bytes(original)
        staged=target.with_suffix(target.suffix+".gait.tmp")
        staged.write_bytes(candidate)
        if read_nef8_scene_records(staged.read_bytes())!=read_nef8_scene_records(candidate):
            raise ValueError("Staged file integrity mismatch: "+relative)
        os.replace(staged,target)
        print(f"APPLIED {relative}: run=2.95, Shift sprint=4.44; original backed up")
if __name__ == "__main__":
    main()
