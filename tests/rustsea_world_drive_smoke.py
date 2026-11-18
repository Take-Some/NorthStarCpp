#!/usr/bin/env python3
"""Exports the REAL NEF8 scene to a transient Node V8-compatible mock fixture.

Tests that game script moves CityRoot entities in world space while preserving
terrain coordinates and animating project-owned drive components by odometry.
Actual grounded character transfer is tested in native Jolt physics_smoke.
"""
from __future__ import annotations
import json
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
PROJECT=ROOT/"Projects/MovingCitiesPrototype"
import sys
sys.path.insert(0,str(ROOT))
from tools.dev.scene_binary import read_nef8_scene_records

def entity_data(raw):
    n=struct.unpack_from("<H",raw,0)[0]
    name=raw[2:2+n].decode("utf8")
    pos=struct.unpack_from("<fff",raw,2+n)
    mass=struct.unpack_from("<f",raw,2+n+12+4+4)[0]
    return {"name":name,"position":dict(zip(("x","y","z"),pos)),"mass":mass}

def main():
    scene=PROJECT/"main.nscn"
    records=read_nef8_scene_records(scene.read_bytes())
    objects=[entity_data(body) for kind,_,body in records if kind==2]
    if len(objects)!=347:raise RuntimeError("Unexpected world drive scene")
    node=shutil.which("node")
    if not node:raise RuntimeError("Node unavailable for JS simulation")
    with tempfile.TemporaryDirectory(prefix="rustsea-qa-") as d:
        fixture=Path(d)/"scene.json"
        fixture.write_text(json.dumps(objects,separators=(",",":")),"utf8")
        test=ROOT/"tests/rustsea_world_drive_smoke.cjs"
        script=PROJECT/"assets/scripts/main.js"
        p=subprocess.run([node,str(test),str(fixture),str(script)],
                         capture_output=True,text=True,timeout=80)
        print(p.stdout[-6000:])
        if p.returncode:
            print(p.stderr[-6000:],file=sys.stderr)
            raise SystemExit(p.returncode)

if __name__=="__main__":main()
