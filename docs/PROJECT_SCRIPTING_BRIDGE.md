# NorthStarCpp project scripting: ECS, scene components, hot reload, distribution

Copyright (C) 2026 Kayla's Systems — Kayla Verner.

## Status and requirements

For the active script-first project architecture, see
[SCRIPT_FIRST_GAMEPLAY_MIGRATION.md](SCRIPT_FIRST_GAMEPLAY_MIGRATION.md).


The current source tree implements the **engine-side integration contracts**.
Real JavaScript execution still requires a native V8 provider compiled for
the active MinGW toolchain (`NORTHSTARCPP_V8_ROOT`). Builds without that provider
can execute only ABI/mock tests, **not JavaScript gameplay**.

Source project/game structure:

```text
MyProject/
  northstarcpp.project.json
  main.nscene
  assets/
  scripts/
    main.js
    behaviors/door.js
```

- `scripts/main.js` is optional. When present, the native Player evaluates it
  through `engine.assets` before the first game frame.
- Scene V3 objects may include `script <stable-entity-id> "scripts/behaviors/door.js"`.
  The record is accepted in any position, validated, and serialized canonically
  after the associated entity line. V1/V2 scenes continue to work.
- The editor supports Scripting > Attach Script Component... and Remove Script
  Component. Scripted objects appear as `[JS]` in the outliner.
- Editor Play mode, and Player source-project mode, invoke script lifecycle events.
- V8 globals are separate per ScriptRuntime; shared behavior script assets are
  evaluated once per runtime, in isolated **lexical** closures.
- `start`, `update` and `stop` are global lifecycle events.
- `entity.start`, `entity.update` and `entity.stop` carry a JSON object with
  `id` (stable decimal ID as **string**), `index`, `script`, and
  `deltaTime` for updates. Scene residency suppresses entity updates for
  unloaded streaming cells. Scripts can match `event.script`.
  Entity events are batched into up to 256 per V8 invocation.
- Source-project hot reload polls script modification times every 500 ms. It
  constructs a **new context**, binds APIs and evaluates source before
  switching. On exceptions, the old runtime remains active. This resets
  JavaScript global state and is not yet a state-preserving HMR implementation.
- Exported game packages copy `scripts/**/*.js` and `scripts/**/*.mjs` and
  require the selected V8 scripting provider. `--packaged-game` disables
  source hot reload. Missing script resources or absent providers fail export.

## Scripted entity scene example

```text
NorthStarCpp.Scene 3
scene "Example"
streaming 150 220 1
entity 1 0 "Cube" "Default" "" "" "" 0 0 0 1 46b9d8 0 0 0 0 0 1 1 1 0 1000000 1 1
script 1 "scripts/behaviors/cube.js"
```

The example must be combined with an otherwise valid scene for the actual
project, including the usual camera/player objects as needed.

## Project entrypoint scripts/main.js

```js
engine.on("start", () => {
    engine.logger.info("Scripting initialized");
    engine.logger.info("Scene objects: " + engine.scene.objectCount());
});

engine.on("update", dt => {
    if (engine.input.isDown(32)) engine.logger.debug("Space is held");
});

engine.on("stop", () => engine.logger.info("Scripting stopped"));
```

## Per-entity script scripts/behaviors/cube.js

```js
const path = "scripts/behaviors/cube.js";
engine.on("entity.start", e => {
    if (e.script === path) engine.logger.info("Entity started: " + e.id);
});
engine.on("entity.update", e => {
    if (e.script !== path) return;
    const pos = engine.ecs.getPosition(e.id);
    // Static objects only; dynamic bodies must be moved via engine.physics.
    // engine.ecs.setPosition(e.id, pos.x + e.deltaTime, pos.y, pos.z);
});
```

## Current host APIs

| Namespace | Implemented methods |
| --- | --- |
| `engine.logger` | `debug`, `info`, `warn`, `error` |
| `engine.perlin` | `sample2(x,y)` |
| `engine.scene` | `objectCount()`, `setLayerVisible(name,bool)` |
| `engine.physics` | `applyImpulse(index,x,y,z)` |
| `engine.input` | `isDown(keyCode)` |
| `engine.ecs` | `entities()`, `query(componentName)`, `has(id,componentName)`, `get(id,componentName)`, `getScript(id)`, `getPosition(id)`, `setPosition(id,x,y,z)` |
| `engine` | `call(capability,method,...args)`, `api(capability)`, `on`, `off`, `emit` |

The ECS bridge uses a runtime `EcsWorld` copy whose `NsEntity` handles are
mapped to stable scene document IDs. The native ECS registers optional northstar.script components from V3 scenes.
The provider's built-in component names are allowlisted; native pointers and arbitrary component bytes are **not**
exposed to JavaScript. `setPosition` accepts only static/non-player bodies,
updates the neutral ECS position and moves the matching physical static body.
Dynamic bodies remain under the physics provider.

## Verification / limitations

- ABI/mock scripting tests had passed; V8-specific tests require a real SDK.
- Scene V3 compilation/roundtrip and game packaging unit tests were run
  independently after the change.
- A complete V8 runtime smoke test, exported game playthrough and frame-time
  measurements remain mandatory before declaring release readiness.
- Script events currently dispatch synchronously on the Player thread.
  Dispatch is batched but high actor counts still require scheduler budgets; V8 deadlines are
  per evaluation, not aggregate per-frame budgets.
- No generic arbitrary plugin command dispatch, script-defined native C++ types,
  debug breakpoints or isolated third-party mod sandbox yet.
