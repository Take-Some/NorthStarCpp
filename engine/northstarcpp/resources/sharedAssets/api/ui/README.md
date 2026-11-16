# NorthStar UI Root Script

Every scripted NorthStarCpp project receives the default UI authoring root:

`sharedAssets/scripts/ui/UI.js`

It is evaluated **once per V8 context**, after the engine SDK bootstrap
and before the project's `scripts/main.js`. A project may override its
implementation with `Projects/<Project>/scripts/ui/UI.js` (the same
virtual path, `scripts/ui/UI.js`) without altering the C++ engine.

The engine does not know about missions, health, inventory, progress,
advertising or game rules. Only this generic native UI operation exists:

```js
engine.ui.apply({ target: "screen", nodes: [...] });
```

The root script registers a project-friendly implementation against
`NorthStar.modules.engine.UI.registerRoot(...)`. The SDK exposes it
through `UI.createRoot(layer)`, alongside the existing lower-level
`UI.mount(layer,nodes)` and `UI.unmount(layer)`.

## Minimal interface for any project

Add this to `scripts/main.js`:

```js
import { UI, Events } from "@northstar/engine";

const hud = UI.createRoot("hud");

Events.on("start", () => {
    hud.panel("background", {
        x: 16, y: 16, width: 340, height: 130,
        padding: 12, gap: 6, background: "#101824de"
    })
    .text("heading", "PLAYER STATUS", {
        height: 28, font_scale: 2, color: "#ffd45bff"
    }, "background")
    .text("health", "HEALTH 100", {
        height: 28, color: "#ffffffff"
    }, "background")
    .progress("progress", 1, {
        width: 280, height: 12
    }, "background")
    .mount();
});

Events.on("update", () => {
    // Values are set by game code, not by the UI engine.
    const health = 74;
    hud.batch(view => {
        view.setText("health", "HEALTH " + health);
        view.setProgress("progress", health / 100);
    });
});

Events.on("stop", () => hud.dispose());
```

Other interfaces can coexist:

```js
const quests = UI.createRoot("quests");
const inventory = UI.createRoot("inventory");
const notifications = UI.createRoot("notifications");

// Each root manages its own screen-space tree.
quests.panel("panel", {width: 400, height: 150})
      .text("objective", "Explore the city", {}, "panel")
      .mount();

quests.setText("objective", "Return to the checkpoint");
quests.unmount();
```

## Available primitives

- `UI.createRoot(name)` creates an isolated project-side UI root.
- `root.panel(id, props, parent?)`
- `root.text(id, text, props?, parent?)`
- `root.button(id, caption, props?, parent?)`
- `root.image(id, virtualAssetPath, props?, parent?)`
- `root.input(id, props?, parent?)`
- `root.progress(id, fraction, props?, parent?)` renders a generic bar.
- `root.set(id, props)`, `root.setText(id, text)`, `root.setProgress(id, fraction)`
- `root.batch(callback)` groups changes into one update.
- `root.remove(id)`, `root.clear()`, `root.mount()`,
  `root.unmount()`, `root.dispose()`.

Properties are generic strings, finite numbers and booleans. Colors use
`#RRGGBBAA` strings. Node names are scoped to the root layer.

## Override and hot reload

A project may create `scripts/ui/UI.js` to replace the shared default root.
Its script should register exactly one implementation:

```js
(() => {
    class ProjectUIRoot {
        constructor(layer) { this.layer = layer; }
        mount() { /* compose UI through NorthStar.modules.engine.UI.mount */ }
        unmount() { /* remove the project layer */ }
    }
    NorthStar.modules.engine.UI.registerRoot(ProjectUIRoot);
})();
```

The loader includes `ui/UI.js` and its flat compatibility alias `UI.js`
in the project-root modification watch list, so source changes trigger the
existing script-context hot reload. An invalid override must fail
initialization rather than silently fall back to a different UI behavior.

## Boundaries

The native renderer currently composites this tree as a **screen-space**
overlay. Render-to-texture and an entity/material attachment contract are
required before a project can display the same UI on a 3D surface (e.g.,
an advertising screen). Creating an `image` node does not, by itself,
implement world-space UI or guarantee image-resource drawing support.

Gameplay values remain project-script state; UI nodes are presentation
objects only. No gameplay-specific native API, flags, ECS components or
hardcoded script services are added by this root.
