> Historical design note (2026-11-15). Superseded by the unified SDK
> refactor of 2026-11-17: the event handler and optional sound mapping now
> live in character/footsteps.js and use Audio.play(). The old
> audio/footsteps.js and player/playerFootsteps.js paths are not loaded.

# NorthStar: root events and player footsteps

Implemented in NorthStarCpp on 2026-11-15.

## Root scripts

- `sharedAssets/scripts/northstar/bootstrap.js` exposes Events.define,
  on/off/once, emit, scope(), stats(), history().
- `sharedAssets/scripts/northstar/events.js` declares the validated
  `player.footstep` event. It loads before project scripts.
- `sharedAssets/scripts/player/playerFootsteps.js` emits player.footstep
  based on distance, physics contact, movement intent and configurable stride.
- `sharedAssets/scripts/audio/footsteps.js` subscribes and plays the relevant
  NEF8/NAC clip through the first-party engine.audio provider.

The existing native V8 `engine.on/off/emit` is the **only** dispatcher.
The JS Events facade does not create a second bus.

## Project usage

```js
import { Events, Logger } from "@northstar/engine";
const subscriptions = Events.scope();
subscriptions.on("player.footstep", step =>
    Logger.debug("Player stepped on " + step.surface));
Events.on("stop", () => subscriptions.dispose());

// Define your own project events:
Events.define("quest.completed", payload =>
    payload && typeof payload.id === "string");
Events.emit("quest.completed", {id: "chapter1"});
```

Standard payload:

```js
{ actor: 0, sequence: 1, surface: "wood", gait: "walk",
  speed: 2.4, position: {x:0,y:0,z:0} }
```

Event history is a bounded 64-event ring of names/sequence numbers, without
arbitrary payload retention. Stats count only events emitted through
`Events.emit`; direct native lifecycle signals are excluded.

## Surface and audio contracts

Native engine.player.ground() performs a physics sphere sweep down from the
player's feet and ignores its own body. The native y=0 floor is supported.
Author a collider layer as `surface:wood`, `surface:metal`,
`surface:grass` etc. The fallback examines object name/asset path and
ultimately uses `stone` if no category is known. With Jolt's optional
Physics Surface ABI v1, a valid NCM2 per-triangle material ID now takes
precedence over collider-level inference. Unsupported providers or untagged
NCM1 collision meshes retain the documented fallback.

Player steps are distance-driven and suppress airborne, teleported,
stationary, paused and uncommanded moving-platform cases. The default audio
subscriber maps surface and gait to
`audio/footsteps/<surface>.nac` and `walk_*` / `run_*` /
`sprint_*` tracks. These are NEF8 audio containers.

The native `engine.audio.playNac(path,prefix,variation,gain)` caches decoded
NEF8/NAC tracks for repeated playback and selects the NorthStar Audio ABI 2
provider. A missing asset/provider produces one warning rather than
suspending all gameplay scripts.

## Validation and limits

- Headless Node root-event/footstep smoke: PASSED.
- Existing targeted player-only and camera-only JS smokes: PASSED.
- Native window.cpp MinGW C++17 syntax check: PASSED.
- First-party native audio provider DLL: BUILT in the available QA directory.
- Full Player link: BLOCKED by existing V8 runtime download SSL CA error.
- In-game audio listening, real material IDs, and frame-time tests: PENDING.

The full gameplay suite also references a missing external animation fixture.
NEF8 v1 currently inflates a whole surface pack on first playback. Per-track
chunked NEF8 v2 and asynchronous prefetch remain future improvements.
Non-scripted projects must enable player scripting to emit JS gameplay events.
