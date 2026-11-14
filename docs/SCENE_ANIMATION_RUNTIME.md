# NorthStarCpp — Scene Transform Animation Runtime

Date: 2026-11-14  
Copyright (c) 2026 Kayla's Systems — Kayla Verner.

## Architecture

The active C++17 engine now includes a **scene-transform clip** module with
project-authored animation resources. It is not the historical relocated
animation system under `modules/foundation/.../compat/animation`.

- Public SDK: `include/northstarcpp/scene_animation.hpp`
- CPU sampling/playback: `modules/host/scene_animation.cpp`
- AssetManager bridge: `modules/host/scene_animation_assets.cpp`
- Player/Editor script bridge: `modules/editor/window.cpp`
- Native tests: `tests/scene_animation_smoke.cpp`
- Script smoke: `tests/gameplay_scripts_smoke.cjs`
- Example: `Projects/NorthStarAnimationLab/`

The CPU service is independent of D3D11, Bullet, Jolt and Flecs implementations.
Stable scene document entity IDs identify the animated objects. An animation
cannot move a dynamic body or the physics-controlled player by writing an
arbitrary transform: such actors require the physics/character animation stack.

### Clip asset format

`animations/lift.anim.json`

```json
{
  "schema": 1,
  "type": "scene-transform",
  "interpolation": "linear",
  "keyframes": [
    {
      "time": 0.0,
      "translation": [0, 0, 0],
      "rotation": [0, 0, 0],
      "scale": [1, 1, 1]
    },
    {
      "time": 1.0,
      "translation": [0, 2.5, 0],
      "rotation": [0, 180, 0],
      "scale": [1.2, 1.2, 1.2]
    },
    {
      "time": 2.0,
      "translation": [0, 0, 0],
      "rotation": [0, 360, 0],
      "scale": [1, 1, 1]
    }
  ]
}
```

The translation and rotation vectors are **additive offsets** to the entity's
transform when playback begins; scale is component-wise multiplied. Each clip
requires 2..8192 strictly increasing keyframes starting at 0 seconds, is
limited to a duration of 3600 seconds and source size of 1 MiB.
Interpolation is `linear` or `step` and sampling clamps at the endpoints.

### Project scripting API

```js
let entity = null;
engine.on("start", () => {
  entity = engine.scene.objects(0, 256)
    .find(object => object.name === "Animated Cube");
  if (entity) {
    engine.animation.play(entity.id, "animations/lift.anim.json", true, 1);
  }
});

engine.on("update", () => {
  // engine.animation.state(entityId) returns:
  // {time, duration, speed, loop, paused, finished}
});

engine.on("stop", () => {
  if (entity) engine.animation.stop(entity.id);
});
```

Methods:

- `engine.animation.play(entityId,path,loop=false,speed=1)` — validates
  source path, VFS size, schema, entity ID and static-body requirement;
- `engine.animation.stop(entityId)` — clears one active instance;
- `engine.animation.pause(entityId,boolean)` — pauses independently of the
  scene/global session;
- `engine.animation.seek(entityId,seconds)` — jumps to a bounded clip time;
- `engine.animation.state(entityId)` — returns a status object or `null`.

Player resolves clip bytes through the currently mounted AssetStore.
Source projects reject redirected filesystem paths; archived projects read
the asset through the same virtual namespace. `SceneAnimationPlayer` limits
simultaneous active tracks and validates finite values and delta-time bounds.

### Rendering / physics contract

Position changes are submitted to `PhysicsScene::move_static()`, which
updates the provider-owned static collision body. An ECS position component
is updated to keep the scene bridge coherent. Partition-unloaded static bodies
do not receive physical relocations while their timeline continues.

Rotation and scale are applied to the **transient render-scene snapshot**,
not the authored scene document. They do not alter the collider's orientation
or size until a dedicated physics shape-update ABI is integrated.
This is not suitable for physical moving doors requiring changing collision
orientation, skeletal NPC animation or gameplay-authoritative ragdoll.

### Distribution

`tools/build_game.py` now stages `animations/**/*.anim.json`, rejects
redirected files, enforces size limits and checks JSON clip family.
`manifest.json` includes `animation_files` and content hashes.
An exporter unit test checks both successful staging and tamper/mismatch rejection.

### Verification

- Standalone C++ native test: **PASS**, including the authored
  `NorthStarAnimationLab/main.nscene` and `lift.anim.json` clip.
- C++17 syntax checks of the new engine module, Player and Editor: **PASS**.
- JavaScript contract smoke for Animation Lab: **PASS** under Node.
- Python game exporter tests: **14/14 PASS** including keyframe validation.
- The isolated native V8 provider test: **1/1 PASS**, but it does not load
  this project in the full engine Player.
- The full CMake-linked Player and frame-by-frame D3D11 runtime preview
  were **not verified**. There are active competing Ninja builds and
  the current validation build cache has no `NORTHSTARCPP_V8_ROOT`.

This module is the first production-oriented **scene animation** layer;
it is not a full character animation pipeline. Full animation requires
imported skeletons, skinning, retargeting, blend spaces, state machines,
root motion, collision synchronisation, GPU skinning and animation editing.
