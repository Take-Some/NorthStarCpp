# NorthStarCpp: active project script migration

Date: 2026-11-17
Scope: active V8 JavaScript under Projects/*/assets/scripts. Historical
migration snapshots and former .ysc sources remain unchanged.

## Overview

| Project | Updated scripting integration |
| --- | --- |
| AbbyPistolDemo | Factory.script(CharacterVisual), Factory.weapon and Factory.prefab; existing authored skeleton reused |
| KaylaNightCity | Native provider calls replaced by Vehicle, Audio, Storage and Rendering; typed menu state and Factory-managed player session |
| MovingCitiesPrototype (Sea of Rust) | Factory.script(PursuitPrototype), Script lifetime, scene-name indexing, typed vehicle/dust/track state |
| NorthStarFirstPersonPlatform | Factory.script/Factory.prefab; ScriptContext-owned spawned entities; character-owned sound mapping |

## Verified corrections

- Sea of Rust computes safe approach speed relative to the moving lead city,
  rather than world-zero speed. The native-like regression reaches an
  interception gap of 46 metres and exercises 185 moving entities and
  134 animated tread or wheel rotations.
- First Person Protocol gates overlapping pistol body/tail audio on the
  presence of independent ABI-2 voices. Legacy single-stream outputs do
  not truncate the initial firearm transient with immediate tail playback.
- First Person Protocol training props, beacons and crate are tracked
  as ScriptContext-owned Entity resources; cleanup runs on stop/failure.
- Character footstep sounds are chosen by the playable character config
  using the real shared NAC assets, never by generic Engine Audio.
- All active projects have strict JS type checking against the same
  engine/northstarcpp/resources/sharedAssets/api/types/northstar-engine.d.ts.

## Regression commands

    python tools/dev/compose_northstar_bootstrap.py --verify --root engine/northstarcpp/resources/sharedAssets/api
    node tests/project_sdk_migration_smoke.cjs
    node tests/gameplay_scripts_smoke.cjs
    node tests/abby_pistol_demo_smoke.cjs
    node Projects/KaylaNightCity/tests/main_menu_smoke.cjs
    node tests/moving_cities_script_smoke.cjs
    node tests/engine_script_framework_smoke.cjs
    node tools/ts_sdk/node_modules/typescript/bin/tsc --project tools/ts_sdk/tsconfig.json --pretty false
    node tools/ts_sdk/node_modules/typescript/bin/tsc --project Projects/AbbyPistolDemo/jsconfig.json --pretty false
    node tools/ts_sdk/node_modules/typescript/bin/tsc --project Projects/KaylaNightCity/jsconfig.json --pretty false
    node tools/ts_sdk/node_modules/typescript/bin/tsc --project Projects/MovingCitiesPrototype/jsconfig.json --pretty false
    node tools/ts_sdk/node_modules/typescript/bin/tsc --project Projects/NorthStarFirstPersonPlatform/jsconfig.json --pretty false

## Known boundaries

- FirstPersonPlatform's legacy training pistol is not yet converted to the
  canonical WeaponController: its prefab lacks calibrated muzzleLocal and
  ejectionLocal socket data. Such conversion should follow in-scene authoring
  and regression, rather than guessing physical weapon transforms.
- KaylaNightCity retains direct Events subscribers for its menu root because
  its existing multi-scene transitions simulate stop/start in one V8 context.
  Underlying transport, audio, storage and renderer calls are canonical.
- Native C++ executable, production dist, GPU scene and player binary have
  not been rebuilt or replaced by this script-only migration.
- All edits are local; no commit or push was executed.
