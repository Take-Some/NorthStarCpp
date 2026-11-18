#!/usr/bin/env python3
"""Safe one-time art pass for the existing Sea of Rust project.

Updates scene/manifest/native art only; preserves gameplay JS, HUD, configs,
scene staging and original backups. All *runtime* assets are project-local.
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.create_moving_cities_prototype import DEST, files
from tools.dev.scene_binary import validate_nef8_scene

BACKUP = DEST/"migration"/"pre_rustsea_native_art_20261117"
UPDATED = ("main.nscn","northstarcpp.project.json","README.md","gameplay/scene_stats.json")
RESOURCES = ("assets/models/rustsea/","assets/materials/rustsea.","assets/textures/rustsea/")
METADATA = ("assets/rustsea_asset_provenance.json","assets/rustsea_native_models.json","assets/rustsea_native_textures.json")

def apply():
    if not DEST.is_dir():
        raise RuntimeError("Existing Sea of Rust project required")
    manifest = json.loads((DEST/"northstarcpp.project.json").read_text(encoding="utf8"))
    if manifest.get("engine")!="NorthStarCpp" or manifest.get("name")!="The Rust Sea - Moving Cities Prototype":
        raise RuntimeError("Refusing unrelated project")
    if BACKUP.exists():
        raise FileExistsError("Art-pass backup already exists; refusing second overwrite")
    generated=files()
    selected={rel: generated[rel] for rel in generated if rel in UPDATED or
              rel.startswith(RESOURCES) or rel in METADATA}
    if len(selected)!=4+25+4+2+3:
        raise RuntimeError("Unexpected native art file count "+str(len(selected)))
    assert validate_nef8_scene(selected["main.nscn"])==614
    assert not any(key.lower().endswith((".obj",".jpg",".png",".fbx",".glb")) for key in selected)
    for rel,body in selected.items():
        if not rel.startswith("assets/") and rel not in UPDATED:
            raise RuntimeError("Unexpected file path: "+rel)
    BACKUP.mkdir(parents=True, exist_ok=False)
    previous=[]
    for rel in selected:
        old=DEST/rel
        if old.exists():
            target=BACKUP/rel
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(old,target)
            previous.append(rel)
    (BACKUP/"previous_files.json").write_text(json.dumps(previous,indent=2)+"\n",encoding="utf8")
    # Backup is completed before first target mutation.
    for rel,body in selected.items():
        dest=DEST/rel
        dest.parent.mkdir(parents=True,exist_ok=True)
        raw=body if isinstance(body,bytes) else body.encode("utf8")
        tmp=dest.with_name(dest.name+".new-art-pass")
        with tmp.open("xb") as output:
            output.write(raw)
            output.flush()
            os.fsync(output.fileno())
        tmp.replace(dest)
    # No writes to common assets or the engine resources tree.
    assert all((DEST/rel).is_file() for rel in selected)
    report={"applied":True,"project":str(DEST),"updated":len(selected),
            "native_ydr":25,"native_ytd":4,"yaml_materials":0,
            "source_xml":str(DEST/"assets/materials/rustsea.ymat.xml"),
            "backup":str(BACKUP),"preserved_gameplay_scripts":True,
            "native_scene_records":validate_nef8_scene((DEST/"main.nscn").read_bytes())}
    print(json.dumps(report,ensure_ascii=False))

if __name__=="__main__":
    apply()
