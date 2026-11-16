#!/usr/bin/env python3
"""Create an independent NorthStarCpp Abby + pistol + native skydome project.

Does not overwrite existing projects. Source assets are mounted through the
current engine VFS; the skydome is copied into the project's asset root.
"""
from __future__ import annotations

import json
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.dev.scene_binary import (
    _text, _vec, pack_listfile, read_nef8_scene_records, validate_nef8_scene,
    scene_entity_references,
)

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Projects" / "NorthStarFirstPersonPlatform"
TARGET = ROOT / "Projects" / "AbbyPistolDemo"
SKY = (
    "models/skydome.ydd",
    "materials/skydome.ymat",
    "textures/skydome.ytd",
)


def entity(name, stable_id, x, y, z, size, color, mass=0.0,
           *, parent=0, visible=True, collision=True, asset=""):
    if stable_id <= 0 or size <= 0 or mass < 0:
        raise ValueError("Invalid scene entity")
    return (
        2, stable_id,
        _text(name) + _vec(x, y, z) +
        struct.pack("<fIfQQ", float(size), color, float(mass), stable_id, parent) +
        _text("Default") + _text("") + _text("") +
        _text(asset) + _text("") + _vec(0, 0, 0) + _vec(1, 1, 1) +
        struct.pack("<ffBB", 0.0, 1000000.0, int(visible), int(collision))
    )


def scene():
    # Clone the *component contracts*, not the original training level,
    # so Pawn v1 / CharacterMotor / CameraRig / Collider stay ABI-correct.
    original = read_nef8_scene_records((SOURCE / "main.nscn").read_bytes())
    pawn_components = [(k, uid, payload) for k, uid, payload in original
                       if k == 7 and uid == 1]
    if len(pawn_components) != 4:
        raise RuntimeError("Source Player Blueprint no longer has 4 components")
    names = []
    for _, _, value in pawn_components:
        n = struct.unpack_from("<H", value)[0]
        names.append(value[2:2+n].decode("utf-8"))
    if set(names) != {
        "northstar.gameplay.pawn",
        "northstar.gameplay.character_motor",
        "northstar.gameplay.camera_rig",
        "northstar.physics.collider",
    }:
        raise RuntimeError("Source player component contracts changed")
    records = [
        (1, 0, _text("Abby Pistol Demo") +
         struct.pack("<fffII", 150., 220., 1., 64, 64)),
        entity("Demo Pawn", 1, 0, .7, -8, 1.5, 0x5DAAC9, 2.0),
    ]
    next_id = 2
    def add(name, x, y, z, size, color, mass=0., **props):
        nonlocal next_id
        records.append(entity(name, next_id, x, y, z, size, color, mass, **props))
        next_id += 1

    # A 50x50m floor with normal Jolt collisions. Body positions use bottom
    # centre; each 10m cubic tile rises exactly to the y=0 walk surface.
    for x in (-20, -10, 0, 10, 20):
        for z in (-20, -10, 0, 10, 20):
            color = 0x253B4E if (x+z)//10 % 2 else 0x314D62
            if x == 0: color = 0x3A697B
            add(f"Deck {x:+03d} {z:+03d}", x, -10, z,
                10, color)

    # Real physical targets so a physics sweep can hit and push them.
    for index, (x, z) in enumerate(
        ((0, 10), (-6, 13), (6, 13), (-3, 19), (3, 19)), 1
    ):
        add(f"Target {index:02d}", x, 0.1, z, 1.2, 0xDB5463, 7)

    # Contrasting cover and low landmarks, safely off the starting route.
    for index, (x, z) in enumerate(
        ((-11, 4), (11, 4), (-12, 17), (12, 17)), 1
    ):
        add(f"Cover {index:02d}", x, 0, z, 2.5, 0x76889D)
    for index, (x, z) in enumerate(
        ((-18, -18), (18, -18), (-18, 18), (18, 18)), 1
    ):
        add(f"Boundary marker {index:02d}", x, 0, z,
            .7, 0xE3BB62)
    # Each subsystem owns its own authored Scene Entity. The Controller is
    # independent of Pawn, while Motor and Camera follow Pawn as child nodes.
    controller_id=next_id
    add("PlayerController",0,.7,-8,.25,0x46B9D8,
        visible=False,collision=False)
    motor_id=next_id
    add("CharacterMotor",0,.7,-8,.2,0x46B9D8,
        parent=1,visible=False,collision=False)
    camera_id=next_id
    add("Camera",0,2.35,-8,.15,0x46B9D8,
        parent=1,visible=False,collision=False)
    # Author the visual once, under Pawn. Its model comes from the shared
    # prefab and the script reuses this stable ID in FPS/TPS.
    mesh_id=next_id
    add("SkeletalMesh",0,.7,-8,1.,0x67A0C0,
        parent=1,visible=True,collision=False,
        asset="characters/abby/models/abby.ydd")
    for record,name in zip(pawn_components,names):
        kind,entity_id,value=record
        if name=="northstar.gameplay.character_motor":entity_id=motor_id
        if name=="northstar.gameplay.camera_rig":entity_id=camera_id
        records.append((kind,entity_id,value))
    def component(entity_id,name,data):
        return (7,entity_id,_text(name)+struct.pack("<II",1,len(data))+data)
    records.append(component(controller_id,
        "northstar.gameplay.player_controller",
        struct.pack("<QI",1,1)))
    records.append(component(camera_id,
        "northstar.gameplay.camera_attachment",
        struct.pack("<Ifff",0,0.,0.,0.)))
    records.append(component(mesh_id,
        "northstar.gameplay.skeletal_mesh",
        b'0 0 0 0 "" "" "prefabs/abby.nprefab"'))
    data = pack_listfile(records,record_versions={2:2})
    assert validate_nef8_scene(data) == len(records)
    assert len(scene_entity_references(data)) == next_id - 1
    return data, next_id - 1


def script_source() -> str:
    # Versioned standalone project script, not generated from another game's
    # mutable main.js. Engine weapon logic is never required.
    return (ROOT / "tools/templates/abby_pistol_demo/scripts/main.js").read_text("utf-8")


def manifest():
    return {
        "schema_version": "1",
        "engine": "NorthStarCpp",
        "name": "Abby Pistol Demo",
        "game_mode": "explore",
        "category": "Games",
        "template": "sandbox",
        "description": "Independent Abby first-person sandbox with a real pistol, NAC sounds, Jolt physics targets and a textured skydome",
        "tags": ["Abby", "First Person", "Pistol", "NAC", "Jolt", "Skydome", "Pawn"],
        "startup_scene": "main.nscn",
        "state": "playable-development",
        "genre": "first-person-shooter-prototype",
        "level": "Skydome Firing Range",
        "asset_root": "assets",
        "scripting": {"runtime": "V8", "entry": "scripts/main.js"},
        "providers": {"engine.physics": "jolt", "engine.render": "d3d12"},
    }


def setup():
    if TARGET.exists():
        raise RuntimeError("AbbyPistolDemo already exists; never overwrite another project")
    temp = TARGET.with_name("_AbbyPistolDemo_creating")
    if temp.exists():
        raise RuntimeError("Staging project directory already exists")
    try:
        temp.mkdir(parents=True)
        data, objects = scene()
        (temp / "main.nscn").write_bytes(data)
        (temp / "northstarcpp.project.json").write_text(
            json.dumps(manifest(),ensure_ascii=False,indent=2)+"\n",
            encoding="utf-8")
        (temp / "scripts").mkdir()
        (temp / "scripts" / "main.js").write_text(script_source(),encoding="utf-8")
        template=ROOT/"tools/templates/abby_pistol_demo"
        src_config=(template/"scripts/playerConfig.js").read_text("utf-8")
        (temp/"scripts/playerConfig.js").write_text(src_config,encoding="utf-8")
        (temp/"prefabs").mkdir()
        name="pistol.nprefab"
        prefab=(template/"prefabs"/name).read_text("utf-8")
        if not prefab.rstrip().endswith("1 0"):
            raise RuntimeError("Weapon prefab must disable collision")
        (temp/"prefabs"/name).write_text(prefab,encoding="utf-8")
        # Characters are shared, reusable, animation-driven resources.
        # They do not belong to a Player, weapon, or individual game project.
        for name in ("abby.nprefab",):
            if not (ROOT/"sharedAssets"/"prefabs"/name).is_file():
                raise RuntimeError("Missing independent character prefab: "+name)
        # Only sky assets are copied: Abby and pistol are resolved from
        # sharedAssets (60+ MiB of character resources), preventing duplication.
        for relative in SKY:
            source=ROOT/"sharedAssets"/relative
            if not source.is_file():
                raise RuntimeError("Missing native skydome asset: "+relative)
            dest=temp/"assets"/relative
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,dest)
        readme=(template/"README.md").read_text("utf-8")
        (temp/"README.md").write_text(readme,encoding="utf-8")
        run_cmd = r"""@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
if defined NORTHSTARCPP_PLAYER_EXE (
  set "PLAYER=%NORTHSTARCPP_PLAYER_EXE%"
) else (
  set "PLAYER=%ROOT%\_qa_editor_mixed\northstarcpp_player.exe"
  if not exist "%PLAYER%" set "PLAYER=%ROOT%\_qa_content_browser_p2\_abby_prefab_isolated\northstarcpp_player.exe"
  if not exist "%PLAYER%" set "PLAYER=%ROOT%\..\cache\qt-pie-icons-build\northstarcpp_player.exe"
)
if not exist "%PLAYER%" (
  echo [ERROR] NorthStarCpp player not found. Build it or set NORTHSTARCPP_PLAYER_EXE.
  exit /b 2
)
set "NORTHSTARCPP_PHYSICS_PROVIDER=jolt"
set "NORTHSTARCPP_RENDER_PROVIDER=d3d12"
cd /d "%ROOT%"
"%PLAYER%" --explore --scene "%~dp0main.nscn" --shared-assets "%ROOT%\sharedAssets" %*
exit /b %ERRORLEVEL%
"""
        (temp/"run.cmd").write_text(run_cmd,encoding="utf-8")
        verify_cmd = r"""@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
cd /d "%ROOT%"
python tools\create_abby_pistol_demo.py --verify
exit /b %ERRORLEVEL%
"""
        (temp/"verify.cmd").write_text(verify_cmd,encoding="utf-8")
        (temp/"jsconfig.json").write_text(
            json.dumps({"compilerOptions":{"checkJs":True,"target":"ES2022",
              "baseUrl":".","paths":{"@northstar/engine":[
                  "../../sharedAssets/api/types/northstar-engine.d.ts"]}},
              "include":["scripts/**/*.js"]},indent=2)+"\n",encoding="utf-8")
        temp.rename(TARGET)
        print(json.dumps({"created":str(TARGET),"scene_objects":objects,
            "sky_assets":list(SKY),"script_bytes":len(script_source())}))
    finally:
        if temp.exists():shutil.rmtree(temp)


def verify():
    if not TARGET.is_dir():
        raise RuntimeError("Project does not exist")
    scene_data=(TARGET/"main.nscn").read_bytes()
    count=validate_nef8_scene(scene_data)
    records=read_nef8_scene_records(scene_data)
    ids={eid for kind,eid,_ in records if kind==2}
    comps=[(eid,value) for kind,eid,value in records if kind==7]
    names_by_id={}
    for eid,value in comps:
        length=struct.unpack_from("<H",value)[0]
        name=value[2:2+length].decode("utf-8")
        names_by_id.setdefault(name,set()).add(eid)
    pawn=names_by_id.get("northstar.gameplay.pawn",set())
    motor=names_by_id.get("northstar.gameplay.character_motor",set())
    camera=names_by_id.get("northstar.gameplay.camera_rig",set())
    controller=names_by_id.get("northstar.gameplay.player_controller",set())
    if (pawn!={1} or len(motor)!=1 or len(camera)!=1 or
        len(controller)!=1 or not motor.isdisjoint(pawn) or
        not camera.isdisjoint(pawn) or not controller.isdisjoint(pawn) or
        not motor.isdisjoint(camera) or len(ids)<38):
        raise RuntimeError("Controller/Motor/Camera must be separate Scene Entities")
    if names_by_id.get("northstar.gameplay.camera_attachment")!=camera:
        raise RuntimeError("Camera Entity missing camera attachment metadata")
    visuals=names_by_id.get("northstar.gameplay.skeletal_mesh",set())
    if len(visuals)!=1 or 1 in visuals:
        raise RuntimeError("Authored SkeletalMesh Entity is missing")
    visual_id=next(iter(visuals))
    visual_entity=next((payload for kind,eid,payload in records
                        if kind==2 and eid==visual_id),None)
    visual_component=next((payload for eid,payload in comps
                           if eid==visual_id and
                           b"northstar.gameplay.skeletal_mesh" in payload),None)
    if (visual_entity is None or
        b"characters/abby/models/abby.ydd" not in visual_entity or
        visual_component is None or
        b"prefabs/abby.nprefab" not in visual_component):
        raise RuntimeError("Authored SkeletalMesh has no Abby visual prefab")
    pistol=(TARGET/"prefabs"/"pistol.nprefab").read_text("utf-8")
    if not pistol.rstrip().endswith("1 0"):
        raise RuntimeError("Invalid optional pistol prefab")
    for name in ("abby.nprefab",):
        content=(ROOT/"sharedAssets"/"prefabs"/name).read_text("utf-8")
        if any(word in content for word in ("northstar.gameplay.pawn",
                "northstar.gameplay.character_motor","camera_rig","pistol.ydd")):
            raise RuntimeError("Character prefab must be model/animation only")
        if 'northstar.animation.default' not in content or "abby.ydd" not in content:
            raise RuntimeError("Character prefab missing native rig/animation: "+name)
    authored=(TARGET/"scripts/main.js").read_text("utf-8")
    config=(TARGET/"scripts/playerConfig.js").read_text("utf-8")
    for action in ("class CharacterVisual","definition.prefab",
            "Scene.setVisualMode","Scene.playAnimation",
            "new PistolWeapon(character)"):
        if action not in authored:
            raise RuntimeError("Missing independent character/weapon composition: "+action)
    for action in ('prefab: "prefabs/abby.nprefab"',):
        if action not in config:
            raise RuntimeError("Demo character must reference reusable prefabs: "+action)
    if any(x in config for x in ("armsPrefab","bodyPrefab","firstPersonPrefab")):
        raise RuntimeError("Weapon configuration must not own character visuals")
    for action in ('"p-fire"','"p-reload"','"p-semiauto-empty"'):
        if action not in config:
            raise RuntimeError("Missing native pistol YCD clip: "+action)
    for rel in SKY:
        if (TARGET/"assets"/rel).read_bytes()!=(ROOT/"sharedAssets"/rel).read_bytes():
            raise RuntimeError("Corrupted skydome: "+rel)
    for rel in ("characters/abby/models/abby.ydd",
                "models/weapon/pistol/pistol.ydd",
                "audio/weapon/pistol/pistol.nac"):
        if not (ROOT/"sharedAssets"/rel).is_file():
            raise RuntimeError("Missing shared resource: "+rel)
    from tools.pack_pistol_xvag import verify as verify_nac
    audio=verify_nac((ROOT/"sharedAssets/audio/weapon/pistol/pistol.nac").read_bytes())
    if audio["tracks"] != 29:
        raise RuntimeError("Pistol NAC missing audio tracks")
    assert manifest()["startup_scene"] == "main.nscn"
    print(json.dumps({"verified":True,"scene_records":count,"scene_objects":len(ids),
        "pawn_id":1,"player_components":len(comps),
        "skydome":True,"pistol_nac_tracks":audio["tracks"]}))


if __name__=="__main__":
    import argparse
    ap=argparse.ArgumentParser()
    ap.add_argument("--verify",action="store_true")
    args=ap.parse_args()
    if args.verify:verify()
    else:setup()
