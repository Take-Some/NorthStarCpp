# NorthStar UI Root Script

Every scripted NorthStarCpp project receives the default UI authoring root:

`engine/northstarcpp/resources/sharedAssets/api/ui/UI.js`

The protected UI implementation is evaluated once per V8 context,
after engine bootstrap and before project scripts. Projects compose
components and documents without replacing this engine-owned service.

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

## Declarative components and hot reload

Use root.render with nested element descriptors and event callbacks.
This validates atomically and avoids native submission for unchanged
trees. Stable IDs preserve event callbacks; reloadFromAsset uses the
same normalizer and keeps runtime overrides.

For file-backed menus, use UI.loadAsset. The Qt editor indexes the
embedded types/northstar-engine.d.ts for autocomplete, including
UiDeclaration, UiDocument and UIRoot.render.

## Boundaries

Screen roots and UI.createSurface world roots share the same
declaration contract. Actual world-space rendering depends on provider
capabilities and project assets; a UI image does not create an entity.

Gameplay values remain project-script state; UI nodes are presentation
objects only. No gameplay-specific native API, flags, ECS components or
hardcoded script services are added by this root.
