# NorthStar: First Person Protocol

A playable first-person training course built on **NorthStarCpp Player Framework**.
The game uses a component-authored Pawn and Camera Rig, the native Jolt
Character Motor, V8 project scripts and the existing ECS, asset and rendering
providers. It does **not** rely on `SceneObject::player` or the previous
Bullet-powered arena.

## What to play

The level is a 60 m × 60 m training platform with physics obstacles and a
four-stage objective sequence. The objectives are implemented in
`assets/scripts/main.js` using the supported `@northstar/engine` SDK:

1. **SIGNAL:** reach the cyan navigation beacon.
2. **PHYSICS:** displace the yellow dynamic crate by at least 0.9 m.
   Use E to push, or collide with the crate while moving.
3. **TRANSIT:** reach the orange navigation beacon.
4. **EXTRACTION:** reach the green extraction beacon and finish the course.

The objective and completion time appear in the **window title** and logs.
The script creates the three beacon markers and physical crate at start.
No mission entities are hard-coded in the C++ runtime.

## Controls

| Key / device | Action |
| --- | --- |
| WASD / gamepad left stick | Character movement relative to camera |
| Mouse / right stick | Look |
| Left Shift | Sprint (Jolt Character Motor) |
| Space / gamepad Jump | Grounded jump |
| E / gamepad interact | Push a nearby dynamic object |
| V | Toggle first-person and third-person views |
| Mouse wheel | Zoom third-person follow distance |
| R | Restart the course |
| Escape / gamepad Start | Pause / resume |
| F10 | Quit |

The player starts in **first-person view**. Camera authoring values are
read from the selected Pawn's `northstar.gameplay.camera_rig` component.
`assets/scripts/playerConfig.js` defines configuration, controls and optional
character assets, but does not create another PlayerController.
`assets/scripts/player/playerMovement.js` forwards input to
`Player.move`, `Player.jump` and `Player.sprint`.
Actual physical movement is owned by Jolt.

## Architecture

```text
Input Provider (Win32/XInput)
      |
@northstar/engine Player API
      |
Native PlayerController (stable Entity ID)
      |
Possess / Unpossess
      |
Pawn entity (NEF8 Scene V3)
      +-- Character Motor --> engine.physics / Jolt
      +-- Capsule Collider
      +-- Camera Rig --> Camera Director
      +-- Visual model / optional animation data
      +-- Gameplay script --> Four-stage training course
```

Only the Pawn component with **Auto Possess Local** is automatically selected
at game startup. Other Pawn entities can coexist; their stable IDs can be
possessed through the native Player API. Project scenes use NEF8 Entity V2
records that do not serialize the old player flag.

Scene: `main.nscn` — 41 authored entities and the local Pawn with stable
Entity ID **1**. The game adds seven runtime entities (three two-part
beacons and one dynamic crate), producing 48 entities at mission start.
The player's spawn is at **(4, 1.2, -12)**.

## Build, run and verify

Build the latest NorthStarCpp Player and required providers from the
current source tree. The development path used for this integration is:

```text
C:\Users\Kayla\Documents\NorthStar\cache\qt-pie-icons-build\northstarcpp_player.exe
```

For a different executable, set the `NORTHSTARCPP_PLAYER_EXE` environment
variable. The launchers force the supported physics backend:

```powershell
.\Projects\NorthStarFirstPersonPlatform\run.cmd
.\Projects\NorthStarFirstPersonPlatform\verify.cmd
```

`run.cmd` opens the game. `verify.cmd` performs a WARP startup/scene check
and real V8/Flecs/Jolt initialization, without validating on-screen game feel.

Developer regression tests:

```powershell
node tests/first_person_protocol_logic_smoke.cjs
python tests/first_person_protocol_runtime_smoke.py C:\Users\Kayla\Documents\NorthStar\cache\qt-pie-icons-build\northstarcpp_player.exe
```

The first test checks every objective and restart deterministically.
The second runs the actual native Player with Jolt, V8 and Flecs, verifying
spawned entities, native Unpossess/Repossess, sprint and orderly exit.

The Jolt movement test is `tests/player_framework_runtime_smoke.cpp`
(CMake target `northstarcpp_player_framework_runtime_smoke`). It simulates
120 physics frames, checks movement and verifies stable identity after
Unpossess/Repossess. This executable must have access to the built Jolt
and Flecs provider DLLs.

## Migration and rollback

The original scene is retained at
`backups/main-pre-pawn-migration-2026-11-16.nscn`; the intermediate
component-migrated version before grounding the spawn is at
`backups/main-before-ground-spawn-2026-11-16.nscn`.
Both are backups, not the current startup scene.

`tools/migrate_fps_scene.cpp` is the one-time converter from earlier
scene records to current NEF8 format with component-owned Pawn metadata.
It validates the serialization round trip before accepting the output.

## Current limitations

- This is a **playable development vertical slice**, not a shipped game:
  in-game mission HUD, environmental storytelling, sound design and final
  level dressing remain to be produced.
- The level is mostly native box geometry. The original optional Abby
  model/animations remain configured, but complete on-screen skeletal
  acceptance was **not** part of this regression.
- On WARP, the renderer may be slow. The full mission was validated
  deterministically and the real V8 lifecycle was validated over a short
  runtime sequence; **manual end-to-end traversal has not been certified**.
- Some optional shared materials may warn about a missing
  `prefabs/tree_animate/textures/tree_animate.ytd` resource. Asset warnings
  do not currently prevent launch.
- Changes remain local in the development tree; production distributions,
  Git commit and push are not part of this update.
