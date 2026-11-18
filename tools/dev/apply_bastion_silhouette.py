#!/usr/bin/env python3
"""Apply Bastion v2 source-generated scene and native art to project-local assets.

Single-use migration. Does not modify core engine resources, player scripts,
HUD, scene behavior scripts or any sharedAssets.
"""
from __future__ import annotations
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.create_moving_cities_prototype import DEST,files
from tools.dev.scene_binary import validate_nef8_scene

BACKUP=DEST/"migration"/"pre_bastion_silhouette_v2_20261117"
SCENE_FILES={"main.nscn","northstarcpp.project.json",
             "gameplay/scene_stats.json","README.md"}

def apply():
    if not DEST.is_dir():raise RuntimeError("Sea of Rust project not found")
    manifest=json.loads((DEST/"northstarcpp.project.json").read_text("utf8"))
    if manifest.get("name")!="The Rust Sea - Moving Cities Prototype":
        raise RuntimeError("Unexpected project; refusing migration")
    if BACKUP.exists():
        raise RuntimeError("Migration backup already exists; refusing second overwrite")
    generated=files()
    stats=json.loads(generated["gameplay/scene_stats.json"])
    if (stats["entities"],stats["components"],stats["bastion_art_models"])!=(595,602,14):
        raise RuntimeError("Unexpected Bastion art scene baseline")
    entries={k:v for k,v in generated.items()
             if k in SCENE_FILES or k.startswith("assets/models/rustsea/") or
                k.startswith("assets/textures/rustsea/") or
                k.startswith("assets/materials/rustsea.") or
                k in ("assets/bastion_art_pass.json",
                      "assets/rustsea_native_models.json",
                      "assets/rustsea_native_textures.json",
                      "assets/rustsea_asset_provenance.json")}
    if sum(k.endswith(".ydr") for k in entries)!=42 or sum(
        k.endswith(".ytd") for k in entries)!=4:
        raise RuntimeError("Missing authored native meshes or texture dictionaries")
    if validate_nef8_scene(generated["main.nscn"])!=602:
        raise RuntimeError("Generated native scene invalid")
    old_hash={}
    BACKUP.mkdir(parents=True)
    for rel in entries:
        dest=DEST/rel
        if not dest.exists():continue
        save=BACKUP/rel
        save.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(dest,save)
        old_hash[rel]=hashlib.sha256(dest.read_bytes()).hexdigest()
    (BACKUP/"previous_sha256.json").write_text(
        json.dumps(old_hash,indent=2)+"\n","utf8")
    for rel,value in entries.items():
        output=DEST/rel
        output.parent.mkdir(parents=True,exist_ok=True)
        raw=value if isinstance(value,bytes) else value.encode("utf8")
        tmp=output.with_name(output.name+".pending_bastion_v2")
        with tmp.open("xb") as f:
            f.write(raw); f.flush(); os.fsync(f.fileno())
        os.replace(tmp,output)
        if hashlib.sha256(output.read_bytes()).digest()!=hashlib.sha256(raw).digest():
            raise RuntimeError("Write checksum mismatch: "+rel)
    print(json.dumps({"applied":True,"project":str(DEST),
                      "updated_files":len(entries),"ydr":42,"ytd":4,
                      "bastion_art_meshes":14,"entities":stats["entities"],
                      "backup":str(BACKUP),"gameplay_scripts_preserved":True}))

if __name__=="__main__":apply()
