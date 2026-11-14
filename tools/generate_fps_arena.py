#!/usr/bin/env python3
# NorthStarCpp — standalone FPS demo project generator
# Copyright (c) 2026 Kayla's Systems — Kayla Verner
"""Generate a reproducible playable target-range arena with a tiled platform."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "Projects" / "NorthStarFPSArena"
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from tools.dev.scene_binary import encode_legacy_template, read_nef8_scene_records


def scene() -> tuple[str, dict[str, int]]:
    records = ["NorthStarCpp.Scene 2"]
    statistics = {"platform_tiles": 0, "border_blocks": 0,
                  "targets": 0, "moving_targets": 0, "hazards": 0,
                  "cover_blocks": 0, "details": 0, "player": 0}

    def cube(group: str, name: str, x: float, y: float, z: float,
             size: float, color: int, *, player: bool = False, mass: float = 0.) -> None:
        if len(name.encode("utf-8")) > 96 or size < 0.05 or size > 100:
            raise ValueError("Invalid FPS scene cube: " + name)
        records.append(
            f'cube "{name}" {x:g} {y:g} {z:g} {size:g} {color:06x} '
            f'{int(player)} {mass:g}')
        statistics[group] += 1

    # Spawn point: in front of the player is +Z (camera yaw=0).
    cube("player", "Player", 0, 0, -8, 1.5, 0x38E0ED, player=True, mass=2.)

    # A 21 x 21 metre floating-looking deck. Every tile is a real static
    # physics body, so collision uses normal provider contracts.
    for x in range(-10, 11):
        for z in range(-10, 11):
            grid = (x + z) % 2
            color = 0x253548 if grid else 0x304457
            if x == 0 or z == 0:
                color = 0x396276 if grid else 0x477F8D
            if abs(x) <= 1 and z == -8:
                color = 0x1FA8C3
            cube("platform_tiles", f"Deck {x+10:02d}-{z+10:02d}", x, -1, z, 1, color)

    # A full-height safety border: player cannot walk off the arena in
    # a regular ground-level route.
    border = set()
    for n in range(-11, 12):
        for x, z in ((-11, n), (11, n), (n, -11), (n, 11)):
            border.add((x, z))
    for x, z in sorted(border):
        for level in range(2):
            cube("border_blocks", f"Perimeter {x:+03d} {z:+03d} L{level}",
                 x, level, z, 1,
                 (0xC34C62 if (x+z) % 7 == 0 else (0x193240 if level else 0x28485A)))

    # Large raised plinths produce sight-line obstruction, not invisible walls.
    for x, z in [(-5,-4),(5,-4),(-5,2),(5,2),(-2,3),(2,3),(-7,7),(7,7)]:
        cube("cover_blocks",f"Cover {x:+03d} {z:+03d}",x,0,z,1.5,0x536779)
        cube("cover_blocks",f"Top {x:+03d} {z:+03d}",x,1.5,z,1.1,0x243E50)

    # All hit targets are colored red and change to green when hit.
    target_positions=[
        ("Alpha",0,7),("Bravo",-4,5),("Charlie",4,5),
        ("Delta",-7,1),("Echo",7,1),("Foxtrot",-3,-1),
        ("Golf",3,-1),("Hotel",-6,8),("India",6,8),
    ]
    for label,x,z in target_positions:
        cube("details", f"Target Base {label}", x, 0, z, 1.0, 0xF0BF64)
        moving = label in ("Charlie", "Echo", "Golf", "India")
        cube("targets", f"Target {'Moving ' if moving else ''}{label}",
             x, 1.05, z, 1.2, 0xFA4765)
        statistics["moving_targets"] += int(moving)

    # Contact hazards exercise health, damage cooldown and game-over/restart.
    for name,x,z in (("A", -3, -4), ("B", 3, -4), ("C", -1, 4)):
        cube("hazards", f"Hazard {name}", x, 0, z, 1, 0xE85C35)

    # Glowing visual guidance strips and towers: game assets are scene geometry.
    for z in (-8,-4,0,4,8):
        for x in (-9,9):
            cube("details",f"Marker {x:+03d} {z:+03d}",x,0,z,0.4,0x24D4DA)
    for x,z in ((-9,-9),(9,-9),(-9,9),(9,9)):
        for level in range(3):
            cube("details",f"Beacon {x:+03d} {z:+03d} {level}",x,level,z,0.6,
                 (0xED526B if level==2 else 0x37687B))
    if len(records)-1 > 1024:
        raise ValueError("Legacy demo scene exceeds 1024-entity V2 limit")
    return "\n".join(records)+"\n", statistics


def project_files() -> dict[str, str | bytes]:
    arena, stats = scene()
    meta = {
        "schema_version": 1,
        "engine": "NorthStarCpp",
        "name": "NorthStar FPS Arena",
        "game_mode": "fps",
        "category": "Games",
        "template": "sandbox",
        "description": "FPS training level with moving targets, jumping, recoil, health and a pause menu.",
        "tags": ["FPS", "Platform", "Arena", "Target Practice"],
        "startup_scene": "main.nscn",
        "scripting": {"runtime": "V8", "entry": "scripts/main.js"},
        "state": "playable-prototype",
        "genre": "first-person-target-range",
        "level": "Floating Platform",
    }
    readme = r"""# NorthStar FPS Arena

Independent FPS game project for NorthStarCpp 0.4.0. The training platform,
walls, cover and targets are physics-enabled scene objects. Nine targets,
four of which move horizontally, are scattered through the arena.

## Textured skydome

The real models/skydome.ydd geometry is loaded through engine.assets
and rendered by D3D11 using UV-mapped textures/skydome.ytd and the native
materials/skydome.ymat@sky_cloud_clear. The dome follows the camera and never
writes depth. These authored resources are bundled under assets/ for
standalone game distribution, not replaced by a procedural placeholder.

## Controls

- WASD or arrows: move, camera-relative
- Mouse: aim with relative input (engine.input)
- Left mouse button: fire (hold for automatic shots), with recoil
- Space: jump when grounded
- R: reload the 12-round magazine (60 reserve rounds)
- Escape / gamepad Start: open or close the pause menu
- Pause menu: Resume, Restart arena, Exit
- Controller: left stick move, right stick aim, A jump, RT fire, X reload

Score, health, ammunition and a weapon silhouette are displayed by the FPS
HUD. Shooting nine targets changes red to green. The orange ground markers
are hazards that decrease health on contact (damage cooldown applies);
at 0 health, use Restart in the game menu. Static and moving targets block
shots behind them; jumps and transforms are implemented through Bullet or
Jolt engine.physics, not manually switched by the project.

## Run in the engine worktree

    .\out\gcc-fps-validation\northstarcpp_player.exe --fps --scene Projects\NorthStarFPSArena\main.nscn

## Export

Run python -m tools.build_game --project Projects/NorthStarFPSArena
--engine-package dist/NorthStarCpp-0.3.0-windows-x64-Release --physics bullet,
or switch to --physics jolt.

## Limitations

Gameplay is a local target-range prototype, not networked. No character
animation, modeled weapon rig, full material textures, enemy AI or audio
production pipeline. HUD weapon is a stylized renderer overlay.
"""
    script = (ROOT/"tools/templates/northstar_fps_arena.js").read_text(encoding="utf-8")
    return {
        "scripts/main.js":script,
        "northstarcpp.project.json":json.dumps(meta,ensure_ascii=False,indent=2)+"\n",
        "main.nscn":encode_legacy_template(arena),
        "README.md":readme,
        "gameplay/arena_stats.json":json.dumps(stats,ensure_ascii=False,indent=2)+"\n",
    }


def run(verify: bool) -> int:
    files = project_files()
    skydome_files = ("models/skydome.ydd", "materials/skydome.ymat",
                     "textures/skydome.ytd")
    if verify:
        for rel in skydome_files:
            canonical = ROOT / "sharedAssets" / rel
            packaged = DEST / "assets" / rel
            if not canonical.is_file() or not packaged.is_file() or canonical.read_bytes() != packaged.read_bytes():
                raise RuntimeError("FPS skydome resource is missing or stale: " + rel)
        for rel, text in files.items():
            path = DEST/rel
            if not path.is_file():
                raise RuntimeError("FPS project is missing " + rel)
            # Project Manager and gameplay tools may extend manifest and README.
            if rel in ("main.nscn", "gameplay/arena_stats.json", "scripts/main.js"):
                if rel == "main.nscn":
                    # Independent zlib implementations may produce different
                    # compressed bytes for identical typed scene records.
                    # Both files are fully integrity-checked before comparison.
                    same = read_nef8_scene_records(path.read_bytes()) == read_nef8_scene_records(text)
                else:
                    same = path.read_text("utf-8") == text
                if not same:
                    raise RuntimeError("FPS project geometry differs: " + rel)
        current = json.loads((DEST/"northstarcpp.project.json").read_text("utf-8"))
        if current.get("game_mode") != "fps" or current.get("engine") != "NorthStarCpp":
            raise RuntimeError("FPS project manifest lost its game mode")
        if current.get("startup_scene") != "main.nscn":
            raise RuntimeError("Unexpected FPS startup scene")
        print(json.dumps({"verified":True,"project":str(DEST), "objects":len(scene()[0].splitlines())-1,
                          "targets":scene()[1]["targets"]}))
        return 0
    if DEST.exists():
        raise RuntimeError("Project already exists; use --verify (never overwrite user edits)")
    for rel,text in files.items():
        file=DEST/rel
        file.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(text, bytes):
            file.write_bytes(text)
        else:
            file.write_text(text,encoding="utf-8")
    for rel in skydome_files:
        canonical = ROOT / "sharedAssets" / rel
        packaged = DEST / "assets" / rel
        packaged.parent.mkdir(parents=True,exist_ok=True)
        packaged.write_bytes(canonical.read_bytes())
    print(json.dumps({"created":True, "project":str(DEST),"objects":len(scene()[0].splitlines())-1,
                      "stats":scene()[1]}))
    return 0

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify",action="store_true")
    args=parser.parse_args()
    try:
        raise SystemExit(run(args.verify))
    except (OSError,RuntimeError,ValueError) as err:
        parser.exit(2,str(err)+"\n")
