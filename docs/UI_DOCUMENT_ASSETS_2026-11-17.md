# NorthStarCpp: UI Document assets (.nui)

> HISTORICAL: v1 JSON documents have been replaced by XML v3.
> Current specification: docs/NUI_XML_V3_SPEC_2026-11-17.md.

> Updated authoring and project-wide UI/2 pipeline: see
> `docs/UI_ASSET_PIPELINE_V2_2026-11-17.md`. This page documents the original
> UI/1 introduction and earlier migration state.

Date: 2026-11-17. Status: implementation integrated in source; GPU/production QA pending.

## Scope

The game UI now has **one declarative asset type**: `.nui` with JSON
serialization and schema `northstar.ui/1`. It is not an alternative rendering
API. The same `engine.ui.native` provider and the existing Vulkan/D3D12 UI
compositors process both UI loaded from assets and UI assembled by scripts.

The Qt Editor's own top menu remains a separate Qt `QMenu/QAction` shell:
it is not a game screen, and is not defined by .nui files.

### Runtime data path

```text
sharedAssets/ui/*.nui (engine defaults)         Project/assets/ui/*.nui
                \                                  /
                 \--- AssetStore layered VFS ------/
                             |
                 engine.ui.readAsset("ui/name.nui")
                             |
              UI.loadAsset(path, { layer, entityId? })
                             |
           UIRoot (validated elements + event bindings)
                             |
                   engine.ui.apply(document)
                             |
                     UiDocument / ABI 1
                             |
                   engine.ui.native provider
                    /                  \
              RGBA screen UI     RGBA world texture
                    |                  |
          D3D12/Vulkan post-FX   Entity world quad
```

Higher-priority project assets shadow the same virtual path in sharedAssets.
The host restricts asset references to safe `ui/*.nui` paths and caps source
size to 256 KiB. The existing native `ui.apply` bridge continues to validate
types, parent ordering, element counts and property types.

### Canonical asset format

```json
{
  "schema": "northstar.ui/1",
  "target": "screen",
  "layer": "mainmenu",
  "nodes": [
    {"id": "panel", "parent": "", "type": "panel",
      "props": {"x": 40, "y": 60, "width": 600, "height": 360,
                "background": "#152338ee"}},
    {"id": "title", "parent": "panel", "type": "text",
      "props": {"text": "MY GAME", "height": 45, "font_scale": 2}},
    {"id": "start", "parent": "panel", "type": "button",
      "props": {"text": "START", "width": 400, "height": 56}}
  ]
}
```

Supported `type`: panel, text, button, image, input. Typed properties are
booleans, finite numbers, strings, or `#RRGGBBAA` colors. Element identifiers
are stable event keys, not pointers. Parents precede their children. A single
UI root supports up to 256 elements.

For `target:"world"`, supply dimension fields `width`, `height`,
`worldWidth`, `worldHeight` in the document and a runtime `entityId`.

### Script usage

```js
import {UI, Session} from "@northstar/engine";

const menu = UI.loadAsset("ui/main_menu.nui", {layer:"main"});
menu.onClick("start", () => Session.loadScene("main.nscn"));
menu.setText("title", "CUSTOM GAME");
menu.mount();

// Later:
menu.unmount(); // hide screen without destroying the .nui asset
```

Runtime script handles behavior, data, transitions and visibility; the file
owns geometry, text, colors, textures, and style properties. Native engine
code contains no gameplay-specific UI definitions.

### Authoring workflow

1. In the Editor open a scene, then `Content > New UI Document...`.
2. A file is created under project `assets/ui/<name>.nui`.
3. Double-click a UI Document in Content Browser.
4. In its modal UI Designer, select elements from the hierarchy, add/remove
   elements, edit ID/type and properties, and inspect the native-provider preview.
5. Save atomically; invalid provider documents are rejected.

Engine-shared assets are visible in Content Browser but are **read-only** in
the editor. Copy a template to project assets (using the same virtual path
to override it), then edit the project copy. Shared files themselves are
intentionally not mutated through ordinary project authoring.

### Migrated project

`Projects/KaylaNightCity/assets/ui/` now contains:
`main_menu.nui`, `pause_menu.nui`, `settings_menu.nui`,
`credits_menu.nui`, `confirm_exit.nui`, `loading.nui`, and
`controls_hud.nui`. The game's `scripts/main.js` now loads these via
`UI.loadAsset`; menu transitions, input actions and runtime text remain in JS.

`sharedAssets/ui/default_menu.nui` is an engine-wide reusable template.
The seven migrated KaylaNightCity documents are also mirrored into
`sharedAssets/ui/` so they are present in the versioned engine assets, even
though the local `Projects/` directory is gitignored. The corresponding
project files shadow these common defaults through the VFS overlay.
The installation source mirror is in
`engine/northstarcpp/resources/sharedAssets/ui/`.

## Verification and limitations

- `node tools/dev/ui_asset_smoke.mjs` exercises 7 project documents, 1
  shared template, tree compilation, screen mounting, and path safety.
- JS SDK and project script syntax checks pass.
- Targeted Qt C++ syntax checks cover the new UI Designer and Content Browser.
- Full release build, in-game GPU pixel comparison, clickable editing canvas,
  undo/redo, automatic disk hot reload, asset-style inheritance, and visual
  preview of packed YTD image pixels are **not** yet accepted.
- The visual designer currently edits a hierarchy/property table, not a
  drag-and-drop canvas. The native raster preview validates layout and
  properties but does not provide a full texture-loaded game scene.
- Existing other game projects must explicitly opt into `UI.loadAsset`
  until converted; the legacy programmatic UI API is retained for compatibility.
