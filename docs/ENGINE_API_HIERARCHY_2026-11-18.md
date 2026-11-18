# NorthStarCpp — Engine API hierarchy and implementation boundaries

Date: 2026-11-18
Status: C++ syntax and JavaScript contract QA passed on the listed targets.
**Not** a production/full-binary release certificate.

## Contract and architectural invariants

The canonical public JavaScript API groups behaviors under the owning
system and the responsibility of each child capability:

```text
engine
├── render
│   ├── provider   (name/version/engineVersion, quality policy)
│   ├── shaders    (catalog/source/queueCompiled/poll)
│   ├── shadows    (state/capabilities)
│   ├── sky        (configure/state/setTimeOfDay/advance/reset)
│   ├── lighting   [future native methods]
│   ├── materials  [future native methods]
│   └── postfx     [future native methods]
├── audio
│   ├── playback   (play/playNac)
│   ├── voices     (supportsVoices/stopVoice/setVoiceGain)
│   ├── spatial    (setVoicePosition)
│   ├── mixers     [future]
│   └── devices    [future]
├── physics
│   ├── bodies     (applyImpulse/setBodyMaterial)
│   ├── queries    (sweepSphere; optional raycast/channel sweep)
│   ├── contacts   (pollContacts)
│   ├── colliders  [future]
│   ├── materials  [future]
│   └── world      [future]
├── scene
│   ├── entities   (spawn/destroy/objects/objectCount/setLayerVisible)
│   ├── transforms (moveRuntime/rotateRuntime/moveStatic/rotateStatic/setColor)
│   ├── animation  (setAnimation/setVisualMode/bone attachment operations)
│   └── prefabs    (read)
├── camera
│   ├── views      (available/mode/setMode/angles/zoom/followDistance)
│   ├── shots      (cut/pushShot/clearShot/clearShots/activeShot/applyPreset)
│   ├── effects    (shake/recoil)
│   └── cinematics (playCutscene/playTrack/stop/pause/seek/state/lensState)
├── input
│   ├── keyboard   (isDown)
│   ├── pointer    (mouseDown/pointerDelta/wheelDelta)
│   ├── gamepads   (gamepad)
│   └── actions    (actionDown/rebindAction/setContextEnabled)
├── player
│   ├── controller (controlledEntity/controlledIndex/possess/unpossess)
│   ├── view       (viewTarget/setViewTarget/resetViewTarget)
│   └── motion     (move/sprint/jump/ground/motion/eye/animationState)
├── ecs
│   └── entities   (entities/query/queryMany/has/get/getScript/getPosition/setPosition)
├── animation
│   ├── clips      (play/stop/pause/seek/state)
│   ├── graphs     [future]
│   └── skeletons  [future]
├── vehicle
│   ├── core       (attach/detach/state/motion/count)
│   ├── drive      (controls/configureRig/powertrainState/selectGear)
│   ├── navigation (waypoint)
│   ├── wheels     (wheelState/wheelPose/wheelCount)
│   ├── seats      (seatCount/seatState/occupy/release)
│   ├── doors      (setDoor/doorCount/doorState)
│   └── provider   (provider)
├── ui
│   ├── documents  (readAsset/apply/hotReloadEnabled)
│   ├── viewport   (setTitle/viewportSize)
│   ├── input      [future]
│   └── widgets    [future]
├── character.visual (load/pose/hide/clear)
├── ai.navigation    (moveTo/stop/isMoving)
├── session.lifecycle (quit/pause/restart)
├── session.scenes    (scene/loadScene)
├── storage.saves     (write/read/exists/remove)
├── math.noise        (sample2)
├── particles.effects (emit; provider-dependent)
├── weaponfx.effects (emitShot)
└── logger             (debug/info/warn/error; already scoped log service)
```

**Important:** A namespace's existence in a V8 proxy does not itself mean
a provider has registered or implemented methods. Missing methods fail
through the host's explicit capability error. In particular,
`lighting`, `materials`, `postfx`, `mixers`, `devices`, full
`world` and several other planned branches **do not claim a finished
public native implementation**.

## Implementation ownership

- Native `ScriptRuntime` registry and plugin dispatch are keyed by
  full capability string and method name, e.g. `engine.audio.voices` +
  `stopVoice`. The API hierarchy is not a duplicate engine.
- `TypedScriptCallbacks` retains strongly typed C++ callback tables,
  routing each method into its semantic namespace at the binding edge.
- Host-owned state, memory and provider selection remain in the existing
  C++ `Viewport`, `PhysicsScene`, `AudioEngine` and other systems.
- Native V8 and developer d8 sidecar allow arbitrary nested namespace
  traversal without a special-case switch for each system.
- Existing shader and sky functions have **canonical nested registrations**:
  `engine.render.shaders.*` and `engine.render.sky.*`. The sky world
  environment remains authoritative; there is no second sun clock.
- Flat public `engine.rendering.*`, `engine.sky.*`, `engine.audio.*`
  and similar methods have been migrated in the active game host;
  stale historical `.inc` sources not included in the active build
  should not be restored as a second registration path.
- A plugin can provide or override a fully qualified child namespace.
  A builtin and an external DLL implementing the same namespace must
  obey the same method contract. Swapping must happen at the existing
  provider lifecycle boundary, not by shadowing JS values alone.

## Examples

```js
const sun = engine.render.sky.state();
const shadows = engine.render.shadows.state();
const supported = engine.render.shadows.capabilities();
const audioId = engine.audio.playback.playNac("audio/ui.nac","click",0,1);
engine.audio.spatial.setVoicePosition(audioId,0,2,5);
const hit = engine.physics.queries.sweepSphere(0,1,0,0,0,5,.3);
const contacts = engine.physics.contacts.pollContacts(128);
engine.player.motion.move(0,1);
const playerId = engine.player.controller.controlledEntity();
engine.ui.documents.apply({target:"screen",nodes:[]});
```

`shadows.state().verifiedOnGpu` remains false until hardware readback
and a visual scene-level comparison justify a positive status.
`engine.render.shadows` currently only supports introspection of
startup configuration: runtime mutation of GPU CSM/PCSS settings is not
falsely exposed as live switching.

## QA performed

- V8 raw bootstrap nested bridge: 21 different paths, stable callable
  identity, direct full-namespace host calls, and event dispatcher PASS
  (`tests/engine_api_nested_namespace_smoke.cjs`).
- Node smoke: render shader catalog, sky lifecycle, generic Script Framework,
  Scene SDK, player motion/event scripting, shared root scripts, UI root,
  KaylaNightCity menu transitions, gameplay and animation templates PASS
  after authoring test providers against the new interface.
- Source-generated `api/northstar/bootstrap.js` composed from the canonical
  manifest and independently authored module files.
- `window_script_bindings_host.cpp` and
  `window_script_bindings.cpp` passed the actual Release compiler command
  with `-fsyntax-only`.
- A full `cmake --build out/build --target northstarcpp_player` run
  exceeded the tool timeout; **clean link, runtime executable startup and
  GPU acceptance have not yet been verified**.
- The `moving_cities_script_smoke.cjs` fixture remains a separate failing
  legacy vehicle-state contract test; do not weaken it merely to claim
  hierarchy migration as production-ready.
- TypeScript type declarations were migrated; a full `tsc` run could
  not be performed because no `tsc` executable was found.

## Next required integration tasks

1. Complete production build and CTest migration smoke against freshly built V8.
2. Register actual render lighting/material/PostFX and audio mixer/device
   child contracts instead of adding no-op JavaScript shims.
3. Validate project inventory, lifecycle and plugin override precedence
   for each nested provider, including external and builtin modules.
4. Capture D3D12/Vulkan and audio gameplay acceptance before release.

Changes remain local; no commit, push or production package overwrite.
