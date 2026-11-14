"""NorthStarCpp FPS readiness gate for a clean Release build.

Does not stage or publish production files. Verifies project templates, Player
startup, ABI/provider resolution and representative engine/Editor CTests.
Writes a machine-readable acceptance report; failed checks fail closed.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from datetime import datetime, timezone

ROOT=Path(__file__).resolve().parents[1]
PROJECTS={
    "fps": ROOT/"Projects/NorthStar_FPS_Test",
    "explore": ROOT/"Projects/NorthStarFirstPersonPlatform",
}
TESTS=(
    "northstarcpp_fps_arena",
    "northstarcpp_fps_player",
    "northstarcpp_fps_ui",
    "northstarcpp_exploration_bullet",
    "northstarcpp_exploration_jolt",
    "northstarcpp_exploration_player",
    "northstarcpp_player_bundled_scripting",
    "northstarcpp_qt_editor_authoring",
    "northstarcpp_qt_editor_player_launch",
    "northstarcpp_provider_inspector_builtin",
    "northstarcpp_provider_inspector_override",
    "northstarcpp_player_provider_inspection",
    "northstarcpp_d3d12_warp",
    "northstarcpp_d3d12_hardware",
)
REQUIRED_ABIS={
    "engine.assets","engine.audio","engine.ecs","engine.physics",
    "engine.input","engine.logger","engine.render","engine.scripting",
    "engine.geometry","engine.collision","engine.time","engine.perlin",
}
class AcceptanceFailure(RuntimeError):
    pass

def execute(args: list[str], cwd: Path, timeout: int=90) -> dict:
    env=os.environ.copy()
    for key in list(env):
        if key.startswith("NORTHSTARCPP_") and key.endswith("_PLUGIN"):
            env.pop(key)
    run=subprocess.run(args,cwd=cwd,env=env,capture_output=True,
                       text=True,errors="replace",timeout=timeout)
    # CTest JSON discovery can be large; preserve it for parsing.
    stdout=run.stdout if "--show-only=json-v1" in args else run.stdout[-7000:]
    return {"command":args,"exit_code":run.returncode,
            "stdout":stdout,"stderr":run.stderr[-3000:]}

def check(result: dict, action: str) -> None:
    if result["exit_code"]!=0:
        raise AcceptanceFailure(f"{action}: exit {result['exit_code']}: "
                                f"{result['stderr'][-700:]}")

def project_check(name: str, root: Path) -> dict:
    manifest=root/"northstarcpp.project.json"
    if not manifest.is_file():
        raise AcceptanceFailure(f"{name}: missing project metadata")
    data=json.loads(manifest.read_text("utf-8"))
    if data.get("engine")!="NorthStarCpp" or data.get("game_mode")!=name:
        raise AcceptanceFailure(f"{name}: wrong engine/game mode metadata")
    scene_rel=Path(data["startup_scene"])
    if scene_rel.is_absolute() or ".." in scene_rel.parts:
        raise AcceptanceFailure(f"{name}: unsafe startup scene path")
    scene=(root/scene_rel).resolve()
    if root.resolve() not in scene.parents or not scene.is_file():
        raise AcceptanceFailure(f"{name}: missing startup scene")
    head=scene.read_bytes()[:4]
    if head not in (b"NSCN",b"NEF8"):
        raise AcceptanceFailure(f"{name}: unsupported binary scene magic: {head!r}")
    assets=(root/data.get("asset_root","assets"))
    if not assets.is_dir():
        raise AcceptanceFailure(f"{name}: missing project asset root")
    return {"project":data["name"],"game_mode":name,
            "scene":str(scene),"scene_magic":head.decode("ascii"),
            "assets":str(assets),"project_schema":data.get("schema_version")}

def inspect_providers(player: Path) -> dict:
    result=execute([str(player),"--inspect-providers"],player.parent,40)
    check(result,"engine provider inspection")
    rows=result["stdout"].splitlines()
    if not rows or rows[0].split("\t")!=[
        "Capability","Requested","Provider","Version","ABI","Source","Status"
    ]:
        raise AcceptanceFailure("Player did not emit the versioned ABI report")
    inventory={}
    for row in rows[1:]:
        fields=row.split("\t")
        if len(fields)!=7:
            raise AcceptanceFailure("Malformed provider report row")
        capability,requested,provider,version,abi,source,status=fields
        inventory.setdefault(capability,[]).append({
            "request":requested,"provider":provider,"version":version,
            "abi":abi,"source":source,"status":status})
    missing=REQUIRED_ABIS-inventory.keys()
    if missing:
        raise AcceptanceFailure(f"Missing required engine APIs: {sorted(missing)}")
    for capability,rows in inventory.items():
        if not any(item["status"]=="OK" for item in rows):
            raise AcceptanceFailure(f"No valid provider for {capability}")
    renders={x["request"] for x in inventory["engine.render"]
             if x["status"]=="OK"}
    if not {"d3d11","d3d12"} <= renders:
        raise AcceptanceFailure("D3D12/D3D11 provider fallback paths are missing")
    return inventory

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",required=True,type=Path)
    parser.add_argument("--report",type=Path)
    args=parser.parse_args()
    build=args.build_dir.resolve()
    if not (build/"CMakeCache.txt").is_file():
        raise AcceptanceFailure("Missing configured CMake build")
    if "CMAKE_BUILD_TYPE:STRING=Release" not in (build/"CMakeCache.txt").read_text(errors="replace"):
        raise AcceptanceFailure("FPS release acceptance requires a Release build")
    output=(args.report or build/"fps-release-acceptance.json").resolve()
    output.parent.mkdir(parents=True,exist_ok=True)
    report={"schema":1,"checked_at_utc":datetime.now(timezone.utc).isoformat(),
            "build_dir":str(build),"status":"failed","projects":{},
            "providers":{},"commands":[],"tests":[]}
    try:
        player=build/"northstarcpp_player.exe"
        if not player.is_file():
            raise AcceptanceFailure("Player executable is missing")
        for name,folder in PROJECTS.items():
            report["projects"][name]=project_check(name,folder)
        report["providers"]=inspect_providers(player)
        for mode,project in report["projects"].items():
            for option in ("--check-scene","--runtime-init-smoke"):
                command=[str(player),"--warp",option,"--scene",project["scene"],
                         "--fps" if mode=="fps" else "--explore"]
                result=execute(command,player.parent,65)
                report["commands"].append(result)
                check(result,f"{mode} {option}")
        ctest=Path("C:/Program Files/CMake/bin/ctest.exe")
        known=execute([str(ctest),"--test-dir",str(build),
                       "--show-only=json-v1"],build,35)
        check(known,"CTest discovery")
        available={item["name"] for item in json.loads(known["stdout"])["tests"]}
        missing=set(TESTS)-available
        if missing:
            raise AcceptanceFailure("Missing FPS acceptance CTests: "+", ".join(sorted(missing)))
        selection="^("+"|".join(map(re.escape,TESTS))+")$"
        tests=execute([str(ctest),"--test-dir",str(build),"-R",selection,
                       "--output-on-failure","--parallel","2"],build,180)
        report["commands"].append(tests)
        check(tests,"FPS + Editor + GPU CTest acceptance")
        report["tests"]=list(TESTS)
        report["status"]="pass"
        print("NorthStarCpp FPS release acceptance PASS")
        print(f"Games: {', '.join(PROJECTS)}, {len(TESTS)} core acceptance tests")
        return 0
    except (AcceptanceFailure,subprocess.TimeoutExpired,ValueError,OSError) as error:
        report["failure"]=str(error)
        print(f"NorthStarCpp FPS release acceptance FAILED: {error}",file=sys.stderr)
        return 1
    finally:
        output.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n",
                          encoding="utf-8")
        print("Acceptance report:",output,flush=True)

if __name__=="__main__":
    sys.exit(main())
