# NorthStar Player Entity Composition — 2026-11-16

## Goal

Remove the old monolithic `player_blueprint.hpp` gameplay implementation.
A Player is a composition of independent, stable-ID Scene V3/NEF8 objects;
camera switches must never create a new character or copy its model.

## Independent entities

```text
World
├── PlayerController       [controller component, no geometry, independent root]
└── Pawn                   [logical/physical capsule, gameplay identity]
    ├── CharacterMotor     [motor settings and physics movement, separate child]
    ├── Camera             [camera attachment + camera rig, separate child]
    └── SkeletalMesh       [authored visual YDD/YMT + Animation Graph]
```

- Controller stores possession by stable Pawn Entity ID, not a copy of Pawn.
- Motor stores `northstar.gameplay.character_motor` on its own Entity. Jolt
  configures the Pawn's physical body from the Motor child settings; the
  logical Motor child follows Pawn without an additional collision body.
- Camera owns `northstar.gameplay.camera_attachment` and
  `northstar.gameplay.camera_rig`. Its real authored scene position is used
  as camera anchor, while camera yaw/pitch/FOV are independent of the model.
- Mesh and animation are visual concerns only; they do not own possession.
- Legacy scenes with Pawn-owned camera/motor payloads remain readable.
- SceneObject visibility is no longer treated as possession validity.
  Invisible logical Pawns can still be controlled.

## Modular headers

Public API:
- `player_blueprint.hpp`: backward-compatible include facade only.
- `player_blueprint_core.hpp`: shared serialization/lookup primitives.
- `player_blueprint_controller.hpp`: controller binding and validation.
- `player_blueprint_motor.hpp`: Motor tuning, instance lookup, serialization.
- `player_blueprint_camera.hpp`: camera attachment settings.
- `player_blueprint_visual.hpp`: independent visual mesh component.
- `player_blueprint_animation.hpp`: animation graph.
- `player_blueprint_factory.hpp`: transactional Scene composition / IDs.
- `player_blueprint_schemas.hpp`: component schema registration.
- `player_character_motor.hpp`: gameplay Motor resolver.
- `player_camera_rig.hpp`: camera rig + world-space camera anchor.
- `player_controller.hpp`: gameplay possession logic only.

## Abby/PistolDemo

Existing `Projects/AbbyPistolDemo/main.nscn` was migrated without deleting
or replacing the project Pawn and existing level objects. Current Scene:
- Pawn: stable ID 1
- Controller: stable ID 41 (independent root)
- Motor: stable ID 42 (child of Pawn)
- Camera: stable ID 43 (child of Pawn)
- Other existing actors remain, including Torus (ID 40)
- 43 total authored Scene objects, 8 Scene component records

Binary backup before migration:
`Projects/AbbyPistolDemo/docs/main_pre_split_2026-11-16.nscn`

The project template generator now writes new, separate objects by default
(42 Scene objects; Controller 40, Motor 41, Camera 42).

Only `prefabs/characters/abby.nprefab` is instantiated by the project. FPS uses
`Scene.setVisualMode(index, 'first_person_arms')` on the same actor and
performs skinned-arm filtering using YMT weights; TPS uses full world mesh.
Pistol bone socket remains attached to the same Abby, changing the weapon
presentation mode without recreating either actor.

Retired unused `sharedAssets/prefabs/abby_first_person.nprefab`;
backup: `_qa_content_browser_p2/abby_first_person_retired_2026-11-16.nprefab`.

## Test evidence

PASS:
- `northstarcpp_player_blueprint_smoke.exe` — five independent entities,
  stable IDs, separate Motor, Scene V3 + NEF8 round trips, Prefab remapping
  and legacy Motor backward compatibility.
- `northstarcpp_player_blueprint_physics_smoke.exe` — Jolt possession,
  Motor/camera offsets synchronized to Pawn, authored off-axis Camera
  offset, camera anchor and animation state.
- `northstarcpp_player_blueprint_qt_smoke.exe` — Qt editor persists
  controller, Motor, camera, skeletal model and animation settings.
- `node tests/abby_pistol_demo_smoke.cjs` — one persistent Abby prefab
  across FPS/TPS, consistent weapon socket and firing gameplay.
- `node tests/character_prefab_animation_smoke.cjs` — reusable NPC without
  separate arm prefab or possession.
- `python tools/create_abby_pistol_demo.py --verify` — migrated source
  Scene and VFS project references.
- Native Scene loader checked both migrated project and fresh generated
  project template.
- Changed `window.cpp` and `viewport.cpp` compiled successfully.
- Linked separate `_qa_content_browser_p2/northstarcpp_player_entity_qa.exe`.

## Known acceptance blocker

A full interactive QA Player launch is **not accepted**. The new QA
executable terminates with Windows process code 0xC0000374 during the
startup path after scene partitioning and around script initialization.
The previous QA Player starts against the new Scene but cannot execute the
new `Scene.setVisualMode` scripting API, as its embedded JavaScript bundle
is older. The failing process was not installed as production Player.

Further work: isolate runtime heap corruption / sidecar bootstrap, rebuild
a coherent Player + scripting provider + renderer dependency set, run a
real on-screen FPS/TPS/weapon-grip visual acceptance test, then the full
Release regression.

The active installed EXE and provider DLLs were not replaced. No Git
commit or push was performed.
