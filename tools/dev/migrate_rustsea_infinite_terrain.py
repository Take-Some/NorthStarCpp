#!/usr/bin/env python3
"""Upgrade only Sea of Rust runtime scene to native infinite streamed terrain.

Keeps all gameplay/native YDR/YTD/vehicle assets and previous state; backs up
any replaced scene, manifest, scripts and project metadata transactionally.
"""
from __future__ import annotations
import hashlib,json,os,shutil,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.create_moving_cities_prototype import DEST,files
from tools.dev.scene_binary import validate_nef8_scene
BACKUP=DEST/"migration"/"pre_infinite_heightfield_20261117"
REPLACE=["main.nscn","northstarcpp.project.json","gameplay/terrain.json",
         "gameplay/scene_stats.json","assets/scripts/main.js",
         "assets/streaming/procedural_terrain.json"]
def run():
    current=json.loads((DEST/"northstarcpp.project.json").read_text("utf8"))
    if current.get("name")!="The Rust Sea - Moving Cities Prototype":
        raise RuntimeError("Refusing to modify unrelated NorthStar project")
    if BACKUP.exists():raise RuntimeError("Already migrated: "+str(BACKUP))
    generated=files()
    stats=json.loads(generated["gameplay/scene_stats.json"])
    if (stats["entities"],stats["components"])!=(347,354):
        raise RuntimeError("Unexpected streaming scene profile")
    if validate_nef8_scene(generated["main.nscn"])!=354:
        raise RuntimeError("Generated NEF8 scene invalid")
    BACKUP.mkdir(parents=True)
    fingerprints={}
    for relative in REPLACE:
        p=DEST/relative
        if p.exists():
            target=BACKUP/relative
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(p,target)
            fingerprints[relative]=hashlib.sha256(target.read_bytes()).hexdigest()
    (BACKUP/"original_sha256.json").write_text(
        json.dumps(fingerprints,indent=2)+"\n","utf8")
    for relative in REPLACE:
        data=generated[relative]
        raw=data if isinstance(data,bytes) else data.encode("utf8")
        p=DEST/relative;p.parent.mkdir(parents=True,exist_ok=True)
        pending=p.with_name(p.name+".infinite_terrain_pending")
        with pending.open("xb") as f:
            f.write(raw);f.flush();os.fsync(f.fileno())
        os.replace(pending,p)
        if hashlib.sha256(p.read_bytes()).digest()!=hashlib.sha256(raw).digest():
            raise OSError("Checksum failure: "+relative)
    print(json.dumps({"ok":True,"scene_entities":347,
                      "scene_records":354,"streaming":"TerrainProvider",
                      "files_updated":len(REPLACE),"backup":str(BACKUP)}))
if __name__=="__main__":run()
