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
node tests/ui_root_script_smoke.cjs
node tests/ui_generic_document_smoke.cjs
node tests/shared_root_scripts_oop_smoke.cjs
node tests/prefab_root_oop_smoke.cjs
python tools/dev/generate_input_typings.py --check
```

Use the CMake target `northstarcpp_player` and the
`northstarcpp_embedded_engine_api` CTest to verify the generated
read-only catalog is present in the actual native runtime.
