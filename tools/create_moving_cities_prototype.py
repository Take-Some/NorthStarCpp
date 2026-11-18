#!/usr/bin/env python3
"""Create a self-contained NorthStar moving-cities vertical-slice blockout.

The player city is kept in a fixed physics frame. A non-colliding target city
is moved visually to simulate relative approach; this is NOT rigidbody city
dynamics. Existing projects and shared assets are never modified.
"""
from __future__ import annotations
import argparse
import json
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from tools.dev.moving_cities_terrain import TerrainSettings, tiles, outcrops, value_noise
from tools.dev.bastion_tread_kinematics import LINK_COUNT,TRACK_X,WHEEL_X,WHEEL_Y,WHEEL_Z,belt_pose
from tools.dev.scene_binary import (
    _text, _vec, pack_listfile, read_nef8_scene_records,
    validate_nef8_scene, scene_entity_references,
)

DEST = ROOT / "Projects" / "MovingCitiesPrototype"
REFERENCE = ROOT / "Projects" / "NorthStarFirstPersonPlatform" / "main.nscn"
START_RANGE = 135.0
DECK_LIFT = 5.7

def entity(name, uid, x, y, z, size, color, *, mass=0., parent=0,
           visible=True, collision=True, asset="", rotation=(0,0,0)):
    if uid <= 0 or not 0.1 <= size <= 1000:
        raise ValueError("invalid geometry: " + name)
    return (2, uid,
        _text(name) + _vec(x, y, z) +
        struct.pack("<fIfQQ", float(size), color, float(mass), uid, parent) +
        _text("Default") + _text("") + _text("") +
        _text(asset) + _text("") + _vec(*rotation) + _vec(1, 1, 1) +
        struct.pack("<ffBB", 0., 1000000., int(visible), int(collision))
    )

def build_scene():
    source = read_nef8_scene_records(REFERENCE.read_bytes())
    required = {
        "northstar.gameplay.pawn", "northstar.gameplay.character_motor",
        "northstar.gameplay.camera_rig", "northstar.physics.collider",
    }
    comps = []
    for kind, uid, payload in source:
        if kind == 7 and uid == 1:
            size = struct.unpack_from("<H", payload)[0]
            name = payload[2:2+size].decode("utf-8")
            if name in required:
                comps.append((name, payload))
    if set(x[0] for x in comps) != required or len(comps) != 4:
        raise RuntimeError("Reference Pawn components changed; rebuild template integration")

    records = [(1, 0, _text("The Rust Sea - Pursuit") +
                struct.pack("<fffII", 150., 220., 1., 64, 64))]
    next_id = 1
    def add(name, x, y, z, size, color, *, lift=True, **kw):
        nonlocal next_id
        # City vehicle collider geometry is authored inside one massive
        # Jolt dynamic compound Body (CityRoot). Separate deck/structure
        # physics cubes would collide with the city and kick it skyward.
        # The scene's YDR components remain project-owned render meshes.
        if name.startswith("BASTION "):
            kw["collision"]=False
        records.append(entity(name, next_id, x, y+(DECK_LIFT if lift else 0), z, size, color, **kw))
        uid = next_id
        next_id += 1
        return uid

    add("Prototype Pawn", 0, 1.15, -12, 1.5, 0x45C2C2, mass=2.)
    # Real Jolt chassis bodies: start as authored collidable/static roots.
    # create_native_vehicle replaces their rigid bodies with a massive
    # dynamic 6DOF wheel-constrained chassis, retaining stable scene IDs.
    add("CITYROOT Bastion", 0, -5.7, 0, 0.25, 0x334455,
        visible=False, collision=True)
    add("CITYROOT Rust Wren", 0, -5.7, START_RANGE, 0.25, 0x334455,
        visible=False, collision=True)
    # Deck visual tiles: massive CityRoot owns their physical deck collider.
    for x in (-15, -9, -3, 3, 9, 15):
        for z in range(-24, 25, 6):
            tint = 0x485663 if (x + z) % 12 else 0x516879
            if x in (-3, 3): tint = 0x64717A
            add("BASTION Deck %s %s" % (x,z), x, -4.85, z, 6, tint,
                visible=False, collision=True)
    # External hull and lookout, underneath deck.
    for x in (-21, 21):
        for z in (-21, -9, 3, 15, 21):
            add("BASTION Track %s %s" % (x,z), x, -5.7, z, 5.5, 0x252E37, visible=False,collision=False)
            add("BASTION Track hub %s %s" % (x,z), x*1.06, -4.4, z, 2.4, 0xC99B46, visible=False, collision=False)
    for x in (-18.3, 18.3):
        for z in range(-24, 25, 8):
            add("BASTION Guard rail %s %s" % (x,z), x, 0, z, 1.65, 0xA57E42,visible=False,collision=False)
    for z in (-26, 26):
        for x in (-15, -9, -3, 3, 9, 15):
            add("BASTION End rail %s %s" % (x,z), x, 0, z, 1.4, 0xA57E42, visible=False, collision=False)
    # Keep a clear central corridor between player start and helm.
    for x,z,stack in ((-11,-12,3),(11,-12,3),(-11,11,2),(11,11,2)):
        for n in range(stack):
            add("BASTION Housing %s %s L%s" % (x,z,n),
                x, n*3.1, z, 3.1, 0x49535E if n < stack-1 else 0xBB8F4A, visible=False)
    for x,z,stack in ((-12,-20,4),(12,-20,4),(-12,19,4),(12,19,4)):
        for n in range(stack):
            add("BASTION Chimney %s %s L%s" % (x,z,n),
                x, n*2.1, z, 2.1, 0x343943 if n<stack-1 else 0xA75C35, visible=False,collision=False)
    for x in (-5,5):
        add("BASTION Boiler %s" % x, x, 0.1, -2, 3, 0x7A5D40,visible=False,collision=False)
        add("BASTION Engine light %s" % x, x, 3.15, -2, 0.5, 0xE6A648,visible=False,collision=False)
    add("BASTION Command plinth", 0, 0, 8, 3, 0x384958,visible=False)
    add("BASTION Helm Console", 0, 3.05, 8, 1.1, 0xE7B24F,visible=False)
    add("BASTION Beacon", 0, 4.3, 8, 0.42, 0x3EE0CA,visible=False,collision=False)
    for x in (-6,6):
        for z in (-25,25):
            add("BASTION Watchlight %s %s" % (x,z), x, 0.15, z, .65, 0xE7B24F,visible=False,collision=False)

    # Native YDR traction-city hull; the collision proxies remain invisible.
    city_visuals=(
        "bastion_outer_hull", "bastion_armor_belt", "bastion_topdeck",
        "bastion_walkway", "bastion_tread_belts",
        "bastion_wheels", "bastion_superstructure",
        "bastion_roofwork", "bastion_steel_ribs", "bastion_amber_marks",
        "bastion_windows", "bastion_pipework")
    for model in city_visuals:
        add("BASTION ART "+model,0,0,0,1.0,0x80756A,
            lift=False,collision=False,
            asset="models/rustsea/"+model+".ydr")
    # Curated machinery imported from Kenney: interior detail, not city form.
    art_pieces=(
        (-13.2,-16.5,"factory_machine",2.3),
        (13.2,-12.5,"factory_machine-fortified",2.5),
        (-12.9,3.5,"factory_pipe-large-valve",2.4),
        (12.6,8.7,"factory_machine-bed",2.4),
        (-13.4,17.8,"factory_hopper-high-round",2.5),
        (12.8,20.2,"city_detail-tank-large",2.5),
        (-13.8,-21.4,"factory_conveyor-long",2.2),
        (13.7,-22.0,"city_shipping-container-a",2.3),
    )
    for i,(x,z,model,size) in enumerate(art_pieces):
        add("BASTION INDUSTRIAL Module %03d %s" % (i,model),
            x,0,z,size,0x66625A,collision=False,
            asset="models/rustsea/"+model+".ydr")

    # Native animated drive: 48 visible grousers per side, 7 wheel hubs per
    # side. Every group has its own world transform; rotation/position are
    # sampled from actual CityRoot odometry, NEVER elapsed animation time.
    for side in (-1,1):
        tag="L" if side<0 else "R"
        for link in range(LINK_COUNT):
            y,z,angle=belt_pose(link,0.)
            add("BASTION TREAD %s %02d" % (tag,link),
                side*TRACK_X,y,z,1.0,0x353331,lift=False,collision=False,
                rotation=(angle,0,0),
                asset="models/rustsea/bastion_tread_link.ydr")
        for wheel,z in enumerate(WHEEL_Z):
            add("BASTION ROADWHEEL %s %02d" % (tag,wheel),
                side*WHEEL_X,WHEEL_Y,z,1.0,0x645344,lift=False,
                collision=False,asset="models/rustsea/bastion_wheel_spokes.ydr")
    # Project-native, alpha-blended dust cutouts: objects start below terrain
    # and are later placed at world-space track contacts by gameplay.
    for i in range(24):
        add("BASTION DRIVE DUST %02d" % i,0.,-20.,0.,3.,
            0x8C7257,lift=False,collision=False,
            asset="models/rustsea/bastion_dust_puff.ydr")

    # Non-colliding enemy geometry is authored into the Scene asset.
    # The project script updates only relative transforms, never deck physics.
    def enemy(name,x,y,z,size,col):
        add("RUSTWREN "+name, x, y, START_RANGE+z, size, col, collision=False)
    for x in (-9,-3,3,9):
        for z in (-12,-6,0,6,12):
            enemy("deck %s %s" %(x,z),x,-6,z,6,0x663C39)
    for x in (-13.5,13.5):
        for z in (-10,0,10):
            enemy("track %s %s" %(x,z), x,-4.5,z,4.6,0x2B2226)
    for x,z,height in ((-6,-7,4),(6,-7,3),(-6,7,4),(6,7,3)):
        for n in range(height):
            enemy("tower %s %s %s" %(x,z,n),x,n*2.3,z,2.3,
                  0x663E42 if n<height-1 else 0xB25A43)
    for z in (-13,13):
        for x in (-9,-3,3,9):
            enemy("armor %s %s" %(x,z),x,0,z,1.7,0xA04D3D)
    enemy("command eye",0,10,2,2,0xE7BD62)
    enemy("bow",0,0,17,4,0x7C4140)

    # No finite authored terrain proxies. The native TerrainProvider drives
    # synchronized PBR render and Jolt triangle chunks at runtime, following
    # CITYROOT Bastion over absolute coordinates (never camera scrolling).
    settings=TerrainSettings(surface_y=0.0)
    cell_count=0
    rock_count=0
    controller_id = add("PlayerController",0,0.7,-12,.25,0x46B9D8,
                        visible=False,collision=False)
    motor_id = add("CharacterMotor",0,0.7,-12,.20,0x46B9D8,
                   parent=1,visible=False,collision=False)
    camera_id = add("Camera",0,2.35,-12,.15,0x46B9D8,
                    parent=1,visible=False,collision=False)
    for name, payload in comps:
        uid = motor_id if name.endswith("character_motor") else (
            camera_id if name.endswith("camera_rig") else 1)
        records.append((7, uid, payload))
    def component(uid,name,body):
        records.append((7,uid,_text(name)+struct.pack("<II",1,len(body))+body))
    component(controller_id,"northstar.gameplay.player_controller",struct.pack("<QI",1,1))
    component(camera_id,"northstar.gameplay.camera_attachment",struct.pack("<Ifff",0,0.,0.,0.))
    binary = pack_listfile(records,record_versions={2:2})
    assert validate_nef8_scene(binary) == len(records)
    assert len(scene_entity_references(binary)) == next_id-1
    return binary, {"entities":next_id-1, "components":len(records),
                    "terrain_cells":cell_count,"terrain_rocks":rock_count,
                    "terrain_seed":settings.seed,"terrain_surface_y":settings.surface_y,
                     "runtime_terrain_streaming":True,
                    "native_rustsea_visuals":sum(1 for kind,uid,blob in records
                        if kind==2 and b"models/rustsea/" in blob),
                     "bastion_art_models":len(city_visuals),
                     "tread_links":LINK_COUNT*2,
                     "roadwheels":len(WHEEL_Z)*2,
                     "drive_dust":24,
                    "target_city_parts":sum(1 for kind,uid,blob in records
                      if kind==2 and blob[2:2+struct.unpack_from("<H",blob)[0]].decode("utf-8").startswith("RUSTWREN "))}

PLAYER_CONFIG = r'''import { Player, Keys } from "@northstar/engine";
// A possessed generic Pawn without a licensed character or weapon model.
Player.configure({
    enabled:true,
    character:{renderedByProject:true,hideInFirstPerson:true},
    camera:{initialMode:"first_person",toggleKey:Keys.V,
            transitionSeconds:0.3,minDistance:0.6,maxDistance:10,wheelStep:0.5},
    look:{mouseSensitivity:0.0024,maxPitch:1.4},
    movement:{jumpKey:Keys.Space,sprintKey:Keys.Shift},
    interaction:{enabled:false},
    weapon:{enabled:false},
    session:{pauseKey:Keys.Escape,exitKey:Keys.F10,
             title:"THE RUST SEA | Moving Cities Prototype"}
});
'''

MAIN_JS = (ROOT / "tools/templates/moving_cities/main.js").read_text(encoding="utf-8")

HUD_XML = r'''<?xml version="1.0" encoding="utf-8"?>
<NorthStarUI version="3" target="screen" layer="mission">
  <Elements>
    <Panel id="panel">
      <Property name="width" type="number" value="670"/>
      <Property name="height" type="number" value="238"/>
      <Property name="x" type="number" value="12"/>
      <Property name="y" type="number" value="12"/>
      <Property name="padding" type="number" value="12"/>
      <Property name="gap" type="number" value="4"/>
      <Property name="background" type="color" value="#161c23e6"/>
    </Panel>
    <Text id="brand" parent="panel">
      <Property name="text" type="string" value="THE RUST SEA / PROTOTYPE 01"/>
      <Property name="height" type="number" value="26"/>
      <Property name="font_scale" type="number" value="2"/>
      <Property name="color" type="color" value="#e7b24fff"/>
    </Text>
    <Text id="stage" parent="panel">
      <Property name="text" type="string" value="BOARD THE BRIDGE"/>
      <Property name="height" type="number" value="35"/>
      <Property name="font_scale" type="number" value="3"/>
      <Property name="color" type="color" value="#f0eeeaff"/>
    </Text>
    <Text id="objective" parent="panel">
      <Property name="text" type="string" value="Reach the helm"/>
      <Property name="height" type="number" value="42"/>
      <Property name="font_scale" type="number" value="2"/>
      <Property name="color" type="color" value="#f0eeeaff"/>
    </Text>
    <Text id="progress" parent="panel">
      <Property name="text" type="string" value="ENGINE 0 m/s"/>
      <Property name="height" type="number" value="27"/>
      <Property name="font_scale" type="number" value="2"/>
      <Property name="color" type="color" value="#e7b24fff"/>
    </Text>
    <Text id="timer" parent="panel">
      <Property name="text" type="string" value="DISTANCE 135 m"/>
      <Property name="height" type="number" value="27"/>
      <Property name="font_scale" type="number" value="2"/>
      <Property name="color" type="color" value="#e7b24fff"/>
    </Text>
    <Text id="controls" parent="panel">
      <Property name="text" type="string" value="WASD WALK I/K THROTTLE E HELM T RESET"/>
      <Property name="height" type="number" value="26"/>
      <Property name="font_scale" type="number" value="1"/>
      <Property name="color" type="color" value="#9db9bfff"/>
    </Text>
  </Elements>
</NorthStarUI>
'''

README = (ROOT / "tools/templates/moving_cities/README.md").read_text(encoding="utf-8")

RUN = r'''@echo off
setlocal EnableExtensions EnableDelayedExpansion
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
if defined NORTHSTARCPP_PLAYER_EXE (
  set "PLAYER=%NORTHSTARCPP_PLAYER_EXE%"
) else (
  set "PLAYER=%ROOT%\out\build\northstarcpp_player_carrier_sync.exe"
  if not exist "!PLAYER!" set "PLAYER=%ROOT%\out\build\northstarcpp_player_cityphysics.exe"
  if not exist "!PLAYER!" set "PLAYER=%ROOT%\out\build\northstarcpp_player_terrainasync.exe"
  if not exist "!PLAYER!" set "PLAYER=%ROOT%\out\build\northstarcpp_player_streaming.exe"
  if not exist "!PLAYER!" set "PLAYER=%ROOT%\out\build\northstarcpp_player.exe"
  if not exist "!PLAYER!" set "PLAYER=%ROOT%\_qa_editor_mixed\northstarcpp_player.exe"
  if not exist "!PLAYER!" set "PLAYER=%ROOT%\..\cache\qt-pie-icons-build\northstarcpp_player.exe"
)
if not exist "%PLAYER%" (
  echo [ERROR] Build a NorthStarCpp Player or set NORTHSTARCPP_PLAYER_EXE.
  exit /b 2
)
set "NORTHSTARCPP_PHYSICS_PROVIDER=jolt"
set "NORTHSTARCPP_RENDER_PROVIDER=d3d12"
set "ASSETS=%ROOT%\engine\northstarcpp\resources\sharedAssets"
if exist "%ROOT%\sharedAssets" set "ASSETS=%ROOT%\sharedAssets"
cd /d "%ROOT%"
"%PLAYER%" --explore --scene "%~dp0main.nscn" --shared-assets "%ASSETS%" %*
exit /b %ERRORLEVEL%
'''

def files():
    blob, stats=build_scene()
    manifest={
        "schema_version":2,"engine":"NorthStarCpp",
        "name":"The Rust Sea - Moving Cities Prototype",
        "game_mode":"explore","category":"Games","template":"sandbox",
        "description":"Moving city pursuit across infinitely streaming world-space heightfield terrain",
        "tags":["Moving Cities","Jolt","V8","Chase","Procedural Terrain","YDR","YTD","YMAT"],
        "startup_scene":"main.nscn","asset_root":"assets",
        "state":"prototype-mobile-city-world-drive","genre":"first-person-chase",
        "level":"Bastion vs Rust Wren",
        "scripting":{"runtime":"V8","entry":"scripts/main.js"},
        "providers":{"engine.physics":"jolt","engine.render":"d3d12"},
        "ui":{"default":"ui/chase_hud.nui","format":"northstar.ui/xml/3",
              "asset_root":"assets"},
    }
    entries = {
      "main.nscn":blob,
      "northstarcpp.project.json":json.dumps(manifest,indent=2)+"\n",
      "assets/scripts/main.js":MAIN_JS,
      "assets/scripts/playerConfig.js":PLAYER_CONFIG,
      "assets/ui/chase_hud.nui":HUD_XML,
      "README.md":README,
      "run.cmd":RUN.replace("\n","\r\n"),
      "gameplay/scene_stats.json":json.dumps(stats,indent=2)+"\n",
      "gameplay/terrain.json":json.dumps({
          "schema":1,"seed":20261117,"terrain_type":"streamed_procedural_heightfield",
          "chunk_width_m":64.0,"radius_chunks":4,"base_height_m":-1.3,
           "vertical_scale_m":5.0,
          "deck_height_m":DECK_LIFT,"tiles":stats["terrain_cells"],
          "outcrops":stats["terrain_rocks"],
          "frame":"world_space","world_streaming":"dynamic_heightfield_chunks"
      },indent=2)+"\n",
    }
    # Package only engine-native render resources. OBJ and JPG are offline
    # build inputs: game projects never reference or ship those intermediates.
    art_root=ROOT/"tools/templates/moving_cities/assets"
    models=list((art_root/"models/rustsea").glob("*.ydr"))
    textures=list((art_root/"textures/rustsea").glob("*.ytd"))
    if len(models)!=45 or len(textures)!=5:
        raise RuntimeError("Prepare native Rust Sea art assets before building")
    required=models+textures+list((art_root/"vehicles").glob("*.nvehicle"))+[
                 art_root/"streaming/procedural_terrain.json",art_root/"materials/rustsea.ymat",
                 art_root/"materials/rustsea.ymat.xml",
                 art_root/"rustsea_asset_provenance.json",
                 art_root/"rustsea_native_models.json",
                 art_root/"rustsea_native_textures.json",
                 art_root/"bastion_art_pass.json"]
    for file in required:
        if not file.is_file():raise FileNotFoundError(str(file))
        entries["assets/"+file.relative_to(art_root).as_posix()]=file.read_bytes()
    return entries

def run(verify=False, upgrade=False):
    entries=files()
    if verify:
        for rel, value in entries.items():
            p=DEST/rel
            if not p.is_file():raise RuntimeError("Project file missing: "+rel)
            raw=p.read_bytes()
            if rel=="main.nscn":
                assert read_nef8_scene_records(raw)==read_nef8_scene_records(value)
            elif rel not in ("README.md","northstarcpp.project.json"):
                assert raw==(value if isinstance(value,bytes) else value.encode("utf-8"))
        manifest=json.loads((DEST/"northstarcpp.project.json").read_text("utf-8"))
        assert manifest["schema_version"]==2 and manifest["startup_scene"]=="main.nscn"
        print(json.dumps({"verified":True,"project":str(DEST),
              "records":validate_nef8_scene((DEST/"main.nscn").read_bytes())}))
        return
    if upgrade:
        if not DEST.is_dir():raise RuntimeError("Existing project required for upgrade")
        manifest_path=DEST/"northstarcpp.project.json"
        current=json.loads(manifest_path.read_text("utf-8"))
        if current.get("engine")!="NorthStarCpp" or \
           current.get("name")!="The Rust Sea - Moving Cities Prototype":
            raise RuntimeError("Refusing to overwrite an unrelated project")
        backup=DEST/"migration"/"pre_procedural_terrain_20261117"
        if backup.exists():raise RuntimeError("Upgrade backup exists; no second overwrite")
        for rel in entries:
            src=DEST/rel
            if src.is_file():
                dst=backup/rel
                dst.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(src,dst)
        for rel,data in entries.items():
            dst=DEST/rel
            dst.parent.mkdir(parents=True,exist_ok=True)
            pending=dst.with_name(dst.name+".new")
            pending.write_bytes(data if isinstance(data,bytes) else data.encode("utf-8"))
            pending.replace(dst)
        print(json.dumps({"upgraded":True,"project":str(DEST),
                          "stats":json.loads((DEST/"gameplay/scene_stats.json").read_text("utf-8"))}))
        return
    if DEST.exists():raise RuntimeError("Refusing to overwrite existing project: "+str(DEST))
    staging=DEST.with_name("_MovingCitiesPrototype_creating")
    if staging.exists():raise RuntimeError("Staging directory already exists")
    try:
        for rel,data in entries.items():
            path=staging/rel
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(data if isinstance(data,bytes) else data.encode("utf-8"))
        staging.rename(DEST)
        print(json.dumps({"created":True,"project":str(DEST),
                          "stats":json.loads((DEST/"gameplay/scene_stats.json").read_text("utf-8"))}))
    finally:
        if staging.exists():shutil.rmtree(staging)

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    options=parser.add_mutually_exclusive_group()
    options.add_argument("--verify",action="store_true")
    options.add_argument("--upgrade",action="store_true")
    args=parser.parse_args()
    run(verify=args.verify,upgrade=args.upgrade)
