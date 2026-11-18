# NorthStarCpp — project-owned UI assets, schema v2 and authoring editor

> HISTORICAL: persisted .nui assets are now XML v3, not JSON.
> See docs/NUI_XML_V3_SPEC_2026-11-17.md.

Date: 2026-11-17
Scope: source implementation (not an installed Release build).

## Architectural decision

The canonical **game UI authoring format is .nui**. The engine.ui provider API
is still the low-level rendering/input contract, but authored game widgets
should be stored in UI assets rather than created inline by game logic.

The *Qt Editor shell* (menus/docks) is an authoring application, not a game UI,
and deliberately remains implemented with Qt. The old standalone Win32 FPS
debug/pause overlay is a known legacy exception; it is **not** yet converted.

- sharedAssets/ui/*.nui: common engine/game UI templates.
- <project>/assets/ui/*.nui: project-owned screens, higher VFS priority.
- <project>/northstarcpp.project.json: ui.default identity.
- scripts/main.js: loads UI via UI.loadAsset; owns callbacks and dynamic state.
- engine.ui.apply: the only host-facing method rendering node documents.
- engine.ui.native / D3D12 / Vulkan: same provider and compositor as before.

## UI/2 asset format

Version 1 remains supported. Version 2 adds local **design tokens** and
**named reusable styles** that are resolved to the same typed properties before
submitting nodes to the native UI provider.

```json
{
  "schema": "northstar.ui/2",
  "target": "screen",
  "layer": "mainmenu",
  "tokens": {
    "accent": "#ffcb64ff",
    "foreground": "#eaf5ffff"
  },
  "styles": {
    "menu_button": {
      "width": 505,
      "height": 60,
      "color": {"token": "foreground"}
    }
  },
  "nodes": [
    {"id": "menu", "parent": "", "type": "panel",
     "props": {"width": 570, "height": 510}},
    {"id": "start", "parent": "menu", "type": "button",
     "style": "menu_button",
     "props": {"text": "START GAME", "color": {"token": "accent"}}}
  ]
}
```

The order of resolution is **named style first, inline node props second**.
Each property may reference a token using `{"token":"name"}`.
The native provider never receives unresolved style/token objects.

The v2 dictionary is bounded to 128 styles and 128 tokens, each style to
48 properties, each document to 256 elements; source is limited to 256 KiB.
Unrecognized styles, tokens or unsafe asset paths are rejected.

## Editor authoring workflow

1. `Content > New UI Document...` creates UI/2 with an empty design system.
2. Double-click `.nui` in Content Browser to open the modal UI Designer.
3. Navigate the widget tree; select a native-preview button to focus its node.
4. Choose a widget's parent, type or reusable style, or edit local properties.
5. `Design System / Styles...` edits style and token dictionaries as JSON.
6. Undo/Redo restores document snapshots (up to 100 changes).
7. Save atomically. The native UI provider validates the submitted document.
8. Shared assets remain read-only in Content Browser; override them in a
   project's `assets/ui` directory.

The designer is **not yet a drag-and-drop WYSIWYG canvas**. Full mouse
geometry manipulation, snap guides, true artboard/viewport sizing, asset atlas
preview, hot reload, advanced animation graphs, component composition and
transitions remain separate unimplemented work.

## Project onboarding and migration

Project Catalog `create()` writes `assets/ui/project_root.nui`,
`scripts/main.js` (loads the document and mounts it on start) and
`ui.default = "ui/project_root.nui"` metadata. It uses UI/2.

Project Catalog `inspect()` now reads and validates the UI virtual path.
The Qt Project Browser displays the declared UI asset next to the status.
Older manifests with no UI entry are still readable and identified as legacy.

Migrated authored game UI:
- KaylaNightCity: main, pause, settings, credits, confirmation, loading,
  and controls HUD.
- AbbyPistolDemo: weapon HUD; runtime text-only updates.
- NorthStarFirstPersonPlatform: weapon HUD and mission HUD; runtime text-only
  updates.
- tools/templates/abby_pistol_demo: copied weapon_hud.nui in demo generator.

These projects no longer call `UI.mount()`, `UI.render()` or
`UI.createRoot()` directly in their application scripts. Those lower-level
JS APIs remain available for compatibility with older or third-party projects
until a separate breaking API transition.

## Verification

- `node tools/dev/ui_asset_smoke.mjs` — 9 shared documents and UI/2
  style/token composition PASS.
- `node tools/dev/ui_project_invariants.mjs` — manifest and authoring
  invariants for all three present projects, Project Manager scaffold and
  Abby template PASS.
- `tests/ui_root_script_smoke.cjs`, `ui_generic_document_smoke.cjs`,
  `embedded_ui_events_smoke.cjs` — PASS.
- `tests/abby_pistol_demo_smoke.cjs`,
  `first_person_protocol_logic_smoke.cjs` — PASS.
- `tests/gameplay_scripts_smoke.cjs --player-only` and `--camera-only`
  — PASS.
- The unspecialized `tests/gameplay_scripts_smoke.cjs` requires a missing
  local `Projects/NorthStarAnimationLab` fixture and is not fully green.
- Targeted C++ syntax checks: Qt UI Designer, Project Catalog,
  Qt Project Browser PASS.
- Production Qt/Player build and a live GPU/pixel comparison **not completed**.
  No installed binaries replaced, and no push or release was performed.

`northstarcpp_ui_asset_documents` is registered under CTest when Node.js is
available. It is self-contained and does not depend on ignored Projects assets.

## Next compatibility boundary

The standalone, non-scripted legacy `--fps` controls in
`modules/editor/window.cpp::create_fps_menu/show_fps_menu` still use Win32
STATIC/BUTTON controls, and the Qt Editor chrome uses native Qt menus.
The latter should remain native; the former needs a dedicated v2 UI
screen and a native event bridge before the old controls can be removed safely.
