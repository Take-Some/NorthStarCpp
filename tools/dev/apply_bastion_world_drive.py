#!/usr/bin/env python3
"""One-time world-drive migration for the existing Sea of Rust project.

Preserves original geometry, gameplay sidecars and user-authored project files,
backs up all replaced resources, checks native YDR/YTD manifests and scene.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.create_moving_cities_prototype import DEST,files
from tools.dev.scene_binary import validate_nef8_scene

BACKUP=DEST/"migration"/"pre_world_drive_20261117"
REPLACE={
    "main.nscn","northstarcpp.project.json",
    "gameplay/scene_stats.json","README.md",
    "assets/scripts/main.js",
}
def apply():
    if not DEST.is_dir():raise RuntimeError("Project missing")
    cfg=json.loads((DEST/"northstarcpp.project.json").read_text("utf8"))
    if cfg["name"]!="The Rust Sea - Moving Cities Prototype":
        raise RuntimeError("Refusing to modify an unrelated project")
    if BACKUP.exists():
        raise FileExistsError("World-drive backup exists; one-time migration only")
    generated=files()
    stats=json.loads(generated["gameplay/scene_stats.json"])
    expected=(727,734,96,14,24)
    observed=(stats["entities"],stats["components"],stats["tread_links"],
              stats["roadwheels"],stats["drive_dust"])
    if observed!=expected or validate_nef8_scene(generated["main.nscn"])!=734:
        raise RuntimeError("Unexpected scene generator result: "+repr(observed))
    selected={key:value for key,value in generated.items()
              if key in REPLACE or key.startswith("assets/models/rustsea/") or
                 key.startswith("assets/textures/rustsea/") or
                 key.startswith("assets/materials/rustsea.") or
                 key in ("assets/bastion_art_pass.json",
                         "assets/rustsea_native_models.json",
                         "assets/rustsea_native_textures.json",
                         "assets/rustsea_asset_provenance.json")}
    if (sum(k.endswith(".ydr") for k in selected),
        sum(k.endswith(".ytd") for k in selected))!=(45,5):
        raise RuntimeError("YDR/YTD packages incomplete")
    if not generated["assets/scripts/main.js"].startswith("// Sea of Rust:"):
        raise RuntimeError("World-drive V8 script missing")
    BACKUP.mkdir(parents=True)
    manifest={}
    for relative in selected:
        path=DEST/relative
        if path.exists():
            old=BACKUP/relative
            old.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,old)
            manifest[relative]=hashlib.sha256(old.read_bytes()).hexdigest()
    (BACKUP/"replaced_assets.json").write_text(json.dumps(manifest,indent=2)+"\n","utf8")
    for relative,value in selected.items():
        path=DEST/relative
        path.parent.mkdir(parents=True,exist_ok=True)
        raw=value if isinstance(value,bytes) else value.encode("utf8")
        tmp=path.with_name(path.name+".world_drive_next")
        with tmp.open("xb") as output:
            output.write(raw);output.flush();os.fsync(output.fileno())
        tmp.replace(path)
        if hashlib.sha256(path.read_bytes()).digest()!=hashlib.sha256(raw).digest():
            raise IOError("Project file checksum mismatch: "+relative)
    print(json.dumps({"ok":True,"updated":len(selected),"project":str(DEST),
                      "native_ydr":45,"native_ytd":5,"entities":727,
                      "backup":str(BACKUP),"shared_assets_modified":False}))
if __name__=="__main__":
    apply()
