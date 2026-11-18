#!/usr/bin/env python3
"""Author a tiny reproducible NPC skinned-animation QA level, no MT28 assets."""
from __future__ import annotations
import argparse,json,shutil
from pathlib import Path
from tools.dev.install_nightcity_npcs import SCENE,ROOT,version_map,name_of,encode_component
from tools.dev.scene_binary import read_nef8_scene_records,pack_listfile,validate_nef8_scene

QA=ROOT/"tests/fixtures/nightcity_npc_animation"
SRC=ROOT/"Projects/KaylaNightCity/assets/animations/npcs"
def main():
 p=argparse.ArgumentParser()
 p.add_argument("--apply",action="store_true")
 args=p.parse_args()
 records=read_nef8_scene_records(SCENE.read_bytes())
 kept={1,*range(3,107),155,156}
 subset=[(kind,eid,raw) for kind,eid,raw in records if
      kind in (1,3) or kind in (2,7) and eid in kept]
 blob=pack_listfile(subset,record_versions=version_map(SCENE.read_bytes()))
 assert read_nef8_scene_records(blob)==subset
 assert validate_nef8_scene(blob)==len(subset)
 # The alternate isolated level samples *walking* YCDs for both NPCs
 # without any transport/gameplay scripts; main.nscn remains idle-only.
 walk_records=[]
 for kind,eid,raw in subset:
  if kind==7 and eid in (155,156) and name_of(raw)=="engine.model.animation":
   slug="ellie" if eid==155 else "isaac"
   raw=encode_component("engine.model.animation",1,
      f"animations/npcs/{slug}_locomotion.ycd@npc-walk".encode("utf8"))
  walk_records.append((kind,eid,raw))
 walk_blob=pack_listfile(walk_records,record_versions=version_map(SCENE.read_bytes()))
 assert read_nef8_scene_records(walk_blob)==walk_records
 assert validate_nef8_scene(walk_blob)==len(walk_records)
 # Single-character scenes remove unrelated streamed geometry and isolate
 # the D3D12/CPU skinner for each mesh independently.
 ellie_records=[r for r in walk_records if r[0] not in (2,7) or r[1]!=156]
 isaac_records=[r for r in walk_records if r[0] not in (2,7) or r[1]!=155]
 ellie_blob=pack_listfile(ellie_records,record_versions=version_map(SCENE.read_bytes()))
 isaac_blob=pack_listfile(isaac_records,record_versions=version_map(SCENE.read_bytes()))
 assert validate_nef8_scene(ellie_blob)==len(ellie_records)
 assert validate_nef8_scene(isaac_blob)==len(isaac_records)
 manifest={"schema_version":"1","engine":"NorthStarCpp",
    "name":"NightCity NPC animation QA","game_mode":"explore",
    "startup_scene":"main.nscn","asset_root":"assets",
    "providers":{"engine.physics":"jolt","engine.render":"d3d12"}}
 sky={"sky":{"mode":"procedural","preset":"sky-act2-day-1-sav-kids-morning",
           "sun":True,"clouds":True,"timeOfDay":9.5,"cloudCoverage":0.40}}
 outputs={QA/"main.nscn":blob,
          QA/"walk.nscn":walk_blob,
          QA/"ellie.nscn":ellie_blob,
          QA/"isaac.nscn":isaac_blob,
          QA/"northstarcpp.project.json":(json.dumps(manifest,indent=2)+"\n").encode(),
          QA/"main.nscn.sky.json":(json.dumps(sky,indent=2)+"\n").encode(),
          QA/"walk.nscn.sky.json":(json.dumps(sky,indent=2)+"\n").encode(),
          QA/"ellie.nscn.sky.json":(json.dumps(sky,indent=2)+"\n").encode(),
          QA/"isaac.nscn.sky.json":(json.dumps(sky,indent=2)+"\n").encode()}
 for slug in ("ellie","isaac"):
  outputs[QA/"assets/animations/npcs"/(slug+"_locomotion.ycd")]=(
      SRC/(slug+"_locomotion.ycd")).read_bytes()
 for path,data in outputs.items():
  if path.exists() and path.read_bytes()!=data:
   raise RuntimeError("Refusing to replace modified QA fixture: "+str(path))
 if args.apply:
  for path,data in outputs.items():
   path.parent.mkdir(parents=True,exist_ok=True)
   if not path.exists():path.write_bytes(data)
 print("PASS" if args.apply else "DRY_RUN",
       "QA scene records",len(subset),"entities",len([x for x in subset if x[0]==2]),
       "native NPC YCDs","2","QA=",QA.relative_to(ROOT))
if __name__=="__main__":main()
