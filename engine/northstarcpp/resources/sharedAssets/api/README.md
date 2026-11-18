# NorthStarCpp — Embedded JavaScript API

This directory is the **authoritative**, engine-owned JavaScript SDK.
`engine/northstarcpp/CMakeLists.txt` embeds its `.js` and `.d.ts` files
directly into `northstarcpp_runtime`; projects do **not** need a writable
copy of engine API scripts in `sharedAssets/scripts`.

All public, named JavaScript imports come from `@northstar/engine`:

```js
import { Scene, Player, UI, Input, Keys, Events, Script } from "@northstar/engine";
```

The import syntax is intentionally restricted to *static named imports*.
The engine's V8 loader resolves them into the current SDK context.
Do not assume Node.js packages, CommonJS `require`, dynamic imports or
general ECMAScript modules are available inside the runtime.

## Root render shader API

Projects use `API.render.shaders` to discover/read shared engine shaders and project-owned `assets/shaders/*`. The shader catalog and source loader work even before renderer initialization.

```javascript
import { API } from "@northstar/engine";
const catalog=API.render.shaders.catalog();
const sky=API.render.shaders.load("shaders/sky/atmosphere.glsl");
const projectShader=API.render.shaders.source("shaders/water.frag");

// Optional material shader hot reload: a separately compiled SPIR-V asset
// must be available and referenced by an active YMAT material.
const count=API.render.shaders.queueCompiled(
    "shaders/water.frag","shaders/water.spv");
const status=API.render.shaders.poll();
```

The canonical built-in sky shader sources are under `sharedAssets/shaders/sky/` and compiled into native providers from those same files. `queueCompiled` does not compile GLSL/HLSL; it queues provider-native binary reload through existing Shader Reload ABI v1/v2. See `docs/ENGINE_RENDER_SHADER_API_2026-11-18.md`.

## Root sky API

Sky is a **top-level engine subsystem** (`api/sky/sky.js`), not a renderer-specific service. Projects use the same root API regardless of D3D12 or Vulkan:

```javascript
import { API } from "@northstar/engine";

// A simple and complete world sky, without creating any scene entity.
API.sky.enable();
API.sky.setTimeOfDay(18, 30);
API.sky.setClouds({coverage:0.55,speed:0.02});
API.sky.setSun({intensity:1.2});
API.sky.setMoon({phase:0.5});
API.sky.setStars({density:0.7});

// Advanced options remain in one configuration object:
API.sky.configure({dayLengthSeconds:1800,latitudeDegrees:42});
const sky = API.sky.state();
```

`import { Sky } from "@northstar/engine"` remains an alias to exactly
the same class (`Sky === API.sky`), so existing games keep working.
The underlying `engine.sky` native bridge is part of the engine root and
accepts a configuration object directly. The JavaScript facade validates
settings and wraps native errors. All celestial and cloud state lives in
the same native `SkyEnvironment`; no independent per-project JS clocks.

## The single Engine API and reusable Factory

The engine exposes one import, @northstar/engine. API is an immutable
singleton instance of the EngineAPI class; EngineModuleRegistry is also a
class. Factory creates/acquires instances using the same host-backed
Scene, Prefab, Vehicle, Camera, Character and WeaponController implementations.
The same APIs remain importable by name for backward compatibility. Project
scripts provide configuration, materials, rigs, scripts, and scene assets; they
must not duplicate collision, weapon ballistics, camera logic or entity creation.

Example (a game script, independent of project layout):

    import { API } from "@northstar/engine";
    const { Factory } = API;
    const existingGate = Factory.existing("Garage Gate", false);
    const crate = Factory.entity({
        name: "Crate", position: {x: 0, y: 2, z: 0}, mass: 10
    });
    const actor = Factory.prefab("prefabs/vehicles/bus.nprefab", {
        position: {x: 5, y: 0, z: 5}
    });
    const vehicle = Factory.vehicle(actor.root.index);
    vehicle.drive({throttle: 0.2, steer: 0, brake: 0});
    vehicle.navigate({x: 300, z: 150, speed: 8, radius: 4});
    const hit = API.collision.traceSphere(
        {x: 0, y: 2, z: 0}, {x: 10, y: 2, z: 0});
    if (API.particles.available("world.dust")) {
        API.particles.emit("world.dust", {
            position: {x: 0, y: 0, z: 0},
            direction: {x: 0, y: 1, z: 0}
        });
    }

Factory.existing acquires an authored entity without spawning a second object.
Factory.prefab uses the existing native prefab reader, parent-before-child
instantiation and transaction rollback. Factory.vehicle returns a non-owning
handle to an existing native vehicle, not another physics vehicle. Factory.camera
and Factory.weapon return controllers; call install() to subscribe them to
the engine lifecycle, or start/update/stop them manually. Factory.create(kind,
descriptor) supports declarative editors. Factory.register("project.kind", fn)
adds an isolated project-specific recipe, returning a disposer; built-in
recipes cannot be overridden in the project registry.

For a player weapon, calling Factory.weapon() preserves the existing Player
input/profile defaults. For an NPC, free object or cinematic weapon, supply a
project-owned profile callback with position (or a holder entity), optionally a
controls callback and an aim callback. The same WeaponController performs
ammunition, fire cadence, collision query, YCD animation, NAC audio, muzzle
VFX, casing ejection and impact. Manual fire() is supported. NPC shots do not
kick the local player's camera by default; opt in via cameraRecoil.

    import { API } from "@northstar/engine";
    const npcGun = API.Factory.weapon({
        profile: () => npcGunDefinition,
        holder: () => npcActor,
        controls: () => ({trigger: npcAI.firing, reload: npcAI.reloading}),
        aim: () => ({
            mode: "sight", eye: npcAI.eye,
            direction: npcAI.lookDirection
        })
    });
    npcGun.install();

The internal Contracts class centralizes basic vector, scalar, index and VFS
validation. It is deliberately not a new physics or ECS implementation.
Camera view controllers use Camera and Input; weapon ballistics use Collision;
weapon muzzle effects use Particles; weapon and casing prefabs use Factory.
All these wrappers delegate to the currently active native provider.

Important native boundaries:
- Generic particle effects require an optional engine.particles provider.
  The existing native muzzle flash is exposed as the weapon.muzzle effect.
  Unavailable providers are reported rather than simulated in JavaScript.
- Collision.traceSphere returns the sphere CENTER at impact, not a world
  surface contact normal. Advanced collision channels require native support.
- Free-weapon barrel mode derives its shot from the native weapon transform,
  not from a guessed player camera or project-specific transform.
- Native Vehicle attach/configureRig is still required before acquiring the
  non-owning Vehicle handle. Rigs remain project-owned .nvehicle assets.

Authored modules:
- core/contracts.js: scalar/vector/VFS validation
- core/factory.js: Factory and EngineAPI class singleton
- core/script.js: Script and PlayerScript lifecycle
- core/sdk.js: immutable EngineModuleRegistry and SDK catalog
- sky/sky.js: root API.sky world sky, clouds, sun, moon, stars, time
- camera/camera.js and camera/controller.js: native camera/view controls
- character/services.js: CharacterService and PlayerService
- scene/scene.js, scene/Entity.js and scene/Prefab.js: scene and handles
- physics/physics.js and physics/collision.js: physics queries
- weapon/weapon.js, weapon/ballistics.js, weapon/effects.js: weapon core
- vehicle/vehicle.js: native vehicle handles
- audio/audio.js: generic WAV/NAC playback
- input/input.js and events/events.js: input and scoped subscriptions
- ui/service.js: UI facade
- session/session.js, player/player.js, particles/particles.js,
  logging/logger.js: session, player, particles and logging services

The authoritative load order is api/manifest.json with domain-relative
source paths. These OOP class files are compiled into one bootstrap and
must never be executed or embedded a second time.

The northstar/bootstrap.js resource is GENERATED from the manifest, not an
independent implementation. Regenerate/verify with
tools/dev/compose_northstar_bootstrap.py --root
engine/northstarcpp/resources/sharedAssets/api [--verify].
The runtime embed tool embeds this exact generated entrypoint.

### Universal sound playback

Audio has no notion of footsteps, firearms, engines, doors or UI sounds.
It accepts a project-owned WAV/NAC path and forwards playback to the
native audio provider. Legacy playWav and playNac remain available.

    import { Audio } from "@northstar/engine";
    Audio.play("audio/ui/confirm.wav");
    Audio.play({asset:"audio/ambience/city.nac",clip:"night",gain:0.65});
    Audio.play("audio/vehicle/motor.nac",{clip:"idle",variation:1});

WAV currently has no provider-level volume/voice parameters, so Audio.play
rejects unsupported WAV options rather than silently ignoring them.

Character-owned contact behavior lives in character/footsteps.js. It emits
the player.footstep event and optionally plays sound through Audio.play.
No sound pack is selected by the engine. A project can supply a sound map
as character.footsteps.sounds (or legacy player.footsteps.sounds), for example:

    footsteps: {
        walkStride: 1.25,
        sounds: {
            metal: {asset:"audio/character/metal.nac",gain:0.8},
            wood: {asset:"audio/character/wood.nac",
                   clips:{walk:"quiet",run:"fast",sprint:"fast"}}
        }
    }

The above values are examples, not engine defaults. An absent map still
allows character contact events without forcing sound playback.

## 1. Quick start: a readable, game-owned HUD

```js
import { UI, Events } from "@northstar/engine";

// This is presentation-only. The project decides what health means.
const hud = UI.createRoot("status");
let health = 100;

Events.on("start", () => {
    hud.group("card", {
        x: 16, y: 16, width: 320, height: 118,
        padding: 12, gap: 4, background: "#15202cdd"
    }, panel => {
        panel.text("heading", "PLAYER STATUS", {
            height: 30, color: "#f2cf67ff", font_scale: 2
        });
        panel.text("value", "HP 100", {height: 28});
        panel.progress("meter", 1, {width: 270, height: 12});
    }).mount();
});

function setHealth(value) {
    health = Math.max(0, Math.min(100, value));
    hud.batch(view => {
        view.setText("value", "HP " + health);
        view.setProgress("meter", health / 100);
    });
}

Events.on("stop", () => hud.dispose());
```

**Why this is simpler:** `group()` scopes all child elements to their
parent. `batch()` sends at most one native UI update, rolls back if its
callback throws, and may contain nested batches. Reapplying identical
values is a no-op. To inspect a live tree, use `hud.snapshot()` and
`hud.has("elementId")`; snapshots are detached copies.

The native API remains generic: `engine.ui.apply({target:"screen",nodes})`.
Nothing in C++ knows about health, missions, progress, inventory, or ads.

### Declarative UI with typed components

Use pure descriptor-producing functions with `UIRoot.render(document)`:

```javascript
import { UI, Events } from "@northstar/engine";

const action = (id, caption, callback) => ({
    id, type: "button", text: caption,
    props: {width: 220, height: 44},
    events: {click: callback}
});
const menu = UI.createRoot("menu");
const show = title => menu.render({
    id: "panel", type: "panel", props: {padding: 12},
    children: [
        {id: "heading", type: "text", text: title},
        action("start", "START", () => startGame())
    ]
}).mount();
Events.on("start", () => show("MAIN MENU"));
Events.on("stop", () => menu.dispose());
```

`render` validates the entire tree before modifying live state. Invalid
declarations do not change the mounted UI, and structurally unchanged
render passes skip native re-submission. Stable IDs retain imperative
event listeners; declarative `events` update with the new document.
`reloadFromAsset` uses the same reconciliation path and preserves
runtime-edited values on retained IDs.

The embedded TypeScript declarations expose `UiDeclaration`, `UiDocument`,
`UiEventName` and `UIRoot.render`. The Qt code editor reads those exact
declarations for autocomplete. Use `Ctrl+Space` or type `menu.re`
after `const menu = UI.createRoot(...)` to see `menu.render`.
This is type-informed, local-editor completion rather than a full TS
language-server implementation.

### UI lifecycle and operations

| Operation | Effect |
| --- | --- |
| `UI.createRoot("hud")` | Allocate a script-owned layer |
| `root.group("panel", props, build)` | Create a panel with scoped children |
| `root.panel/text/button/image/input(...)` | Create generic elements |
| `root.progress(id, fraction, props)` | Compose a visual progress bar |
| `root.set(id, props)` | Update only changed properties |
| `root.setText(id, text)` | Update a text element |
| `root.setProgress(id, fraction)` | Set fraction 0..1 |
| `root.batch(fn)` | Atomic, coalesced batch; supports nesting |
| `root.has(id)`, `root.snapshot()` | Inspect a script-owned UI tree |
| `root.mount()`, `root.unmount()` | Publish or remove a layer |
| `root.remove(id)`, `root.clear()`, `root.dispose()` | Remove children, reset, release |
| `UI.hasLayer(name)` | Check if a layer is currently registered |
| `UI.clear()` | Clear all script-managed layers |

Colors use `#RRGGBBAA`. UI property values are strings, booleans or
finite numbers. Duplicate element identifiers, invalid parents, excessive
node counts and malformed values throw descriptive errors before the
renderer is asked to draw anything.

**Supported scope:** screen-space UI. A button element is a visual
primitive, not a guarantee of interactive click routing. Images depend on
the selected UI provider's resource support. World-space UI on a model
requires a separate render-to-texture/material-attachment contract.

## 2. Scene: safe object lookup and ownership

```js
import { Scene, Player } from "@northstar/engine";

// These throw clear messages when a required entity is missing.
const switchEntity = Scene.requireByName("Entrance Switch");
const entityAtIndex = Scene.require(12);

// Soft lookups remain available for optional objects.
const maybeBeacon = Scene.findByName("Beacon");
const controlledPawn = Player.requirePawn(); // native possession, not player=true
if (Player.hasPawn) {
    // Player.index is the native controlled scene index.
}
```

`Scene.findPawn()` resolves the entity possessed by the native
`PlayerController`. `Scene.findPlayer()` is a compatibility alias, not a
serialized player boolean. `Scene.spawn()` returns a typed `Entity`
handle supporting `setPosition()`, `setRotation()`, `applyImpulse()`
and `destroy()`.

For a full hierarchy, `Prefab.instantiate(path, {position})` performs
dependency validation before native creation and rolls back objects on
failure. For custom physics, the Jolt provider owns the actual motor,
collider and simulation.

## 3. Input: avoid duplicate per-frame logic

```js
import { Input, Keys, GamepadButtons } from "@northstar/engine";

const use = Input.createAction(Keys.E, GamepadButtons.Interact);

function update() {
    const gamepad = Input.firstGamepad(); // sample once per update
    if (use.pressed(gamepad)) {
        // One event per key press, not every held frame.
    }
    const x = Input.axis(Keys.D, Keys.A, gamepad?.leftX ?? 0,
        GamepadButtons.DPadRight, GamepadButtons.DPadLeft, gamepad);
    const y = Input.axis(Keys.W, Keys.S, gamepad?.leftY ?? 0,
        GamepadButtons.DPadUp, GamepadButtons.DPadDown, gamepad);
    const move = Input.normalized2D(x, y);
    // Move using player script policies; physics remains provider-owned.
}
```

`Input.normalized2D` preserves small analogue values but clamps the
diagonal magnitude to one. It throws if either component is non-finite.

## 4. Script lifecycle

```js
import { PlayerScript, Events } from "@northstar/engine";

class BeaconSystem extends PlayerScript {
    #scope = null;

    start() {
        if (!this.active) return;
        this.#scope = Events.scope();
        this.#scope.on("player.footstep", event => {
            // Example only: decide what to do in project code.
        });
    }

    update(dt) {
        if (!this.playing) return;
    }

    stop() {
        this.#scope?.dispose();
        this.#scope = null;
    }
}
new BeaconSystem().install();
```

`Script` handles start/update/stop event subscriptions;
`PlayerScript` adds native Pawn/paused/config access. Explicit
`uninstall()` removes owned subscriptions. `Events.scope()` manages
project subscriptions without creating a second event bus.

## 5. Discoverability

The authoritative declarations for code completion and diagnostics are
`types/northstar-engine.d.ts`. The `input/Keys.js` and
`input/GamepadButtons.js` values are mirrored through
`tools/dev/generate_input_typings.py`; changes to those constants should
be followed by the generator and its `--check` test.

All root scripts are **project-agnostic**. The UI root (`scripts/ui/UI.js`) and Character base (`scripts/character/Character.js`) are sealed engine infrastructure. Player, Camera and other gameplay policy roots remain overrideable via the project VFS. Projects customize interfaces by composing `UI.createRoot()` instances, not replacing the built-in UI class.

## 6. Verification

From the NorthStarCpp root directory:

```powershell
node tests/embedded_api_usability_smoke.cjs
node tests/engine_api_factory_smoke.cjs
node tests/ui_root_script_smoke.cjs
node tests/ui_generic_document_smoke.cjs
node tests/shared_root_scripts_oop_smoke.cjs
node tests/prefab_root_oop_smoke.cjs
python tools/dev/generate_input_typings.py --check
```

Use the CMake target `northstarcpp_player` and the
`northstarcpp_embedded_engine_api` CTest to verify the generated
read-only catalog is present in the actual native runtime.
