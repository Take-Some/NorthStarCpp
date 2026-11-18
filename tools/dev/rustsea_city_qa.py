#!/usr/bin/env python3
"""Execute Sea of Rust's real Jolt+V8 city motion acceptance."""
from __future__ import annotations
import os,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
exe=root/"out/build/northstarcpp_player_cityphysics.exe"
scene=root/"Projects/MovingCitiesPrototype/main.nscn"
shared=root/"engine/northstarcpp/resources/sharedAssets"
args=[str(exe),"--runtime-init-smoke","--explore","--scene",str(scene),
      "--shared-assets",str(shared)]
env=os.environ.copy()
env["NORTHSTARCPP_RUSTSEA_DRIVE_QA"]="1"
p=subprocess.run(args,cwd=root,env=env,stdout=subprocess.PIPE,
                 stderr=subprocess.STDOUT,timeout=115,text=True,errors="replace")
log=root/"out/build/rustsea_city_motion_qa.log"
log.write_text(p.stdout,"utf8")
print("return_code",p.returncode,"log",str(log),"bytes",len(p.stdout))
for line in p.stdout.splitlines():
    if any(key in line for key in ("rustsea.drive.","city yaw exceeded",
                                   "[ERROR]","phase=summary","[project.script][warn]")):
        print(line[:1000])
sys.exit(p.returncode)
