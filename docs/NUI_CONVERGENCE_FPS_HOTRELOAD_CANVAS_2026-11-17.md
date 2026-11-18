# NorthStarCpp — .nui convergence: legacy FPS, hot reload and canvas

> HISTORICAL: the UI asset source format has since moved to XML v3.
> See docs/NUI_XML_V3_SPEC_2026-11-17.md.

**Date:** 2026-11-17
**Scope:** Source implementation. Production build/GPU parity pending.

## Single runtime UI path

The standalone FPS pause screen no longer uses child Win32 STATIC/BUTTON HWNDs.
It uses `ui/fps_pause.nui`, a `northstar.ui/2` screen asset with style tokens,
and the same `Viewport::set_ui_document` and
`Viewport::take_ui_events` path as scripted UI.

Pipeline:

```text
sharedAssets/ui/fps_pause.nui
    + optional project/assets/ui/fps_pause.nui override
       -> AssetStore layered VFS
       -> window_fps_nui.hpp UI/1+UI/2 decoding
       -> typed UiDocument
       -> Viewport::set_ui_document
       -> engine.ui.native -> Vulkan / D3D12 UI compositor
       -> Viewport::take_ui_events
       -> standalone FPS action routing on the game thread
           resume / restart / exit
```

The old Win32 pause widgets, their HWND handles and WM_COMMAND button
routing were removed. `--fps-ui-smoke` now tests the same C++ action
handler used for native UI events. The standalone FPS path does not need V8.

## Unified hot reload

**Scripted UI:** `UI.loadAsset` tracks its source bytes and live `UIRoot`.
When a development Player exposes `engine.ui.hotReloadEnabled()==true`,
`ui.update` polls loaded documents every 500 ms.
`UI.reloadChangedAssets()` is also publicly callable for deterministic
tests. Changes replace the authored element tree *in place* while retaining
event callbacks and runtime `UIRoot.set/setText` overrides. Invalid documents
are logged and the last valid tree remains active. The same logic is mirrored
in both runtime-bootstrap copies and the TS SDK typing.

**Standalone FPS:** the Player checks the mounted local
`fps_pause.nui` source timestamp every 500 ms, then loads a candidate
through the mounted `AssetStore`. Failures warn and keep the last valid UI.
Hot reload is disabled for packaged/archived games.

Because the runtime uses the mounted VFS, project assets override engine
defaults. No user-provided absolute disk path crosses the script bridge.

## Designer canvas

`qt_native_ui_preview.hpp` now supports native rectangle hit-testing, click
selection and mouse drag feedback. The .nui editor offers a **Snap 8 px**
checkbox, and writes drag displacement into authored `x/y` properties.
A golden dashed rectangle shows the in-progress target. The editor keeps
Undo/Redo, parent/style selection, and atomic QSaveFile saving.

Token-bound coordinates are not silently overwritten during drag.
No independent Qt approximation of the game renderer was added.

## Adaptive layout

The native layout engine already supported column/row/grid, percentage
width and height, min/max constraints, scroll offsets and right/bottom
anchors. This change adds:

- `anchor_center_x: true`
- `anchor_center_y: true`
- `x_percent: <0..100>`
- `y_percent: <0..100>`

The FPS pause card now centers relative to its actual clipped parent viewport
instead of using a hardcoded screen-center pixel offset.

## Tests

- `tests/fps_nui_asset_smoke.cpp`: actual UI/2 parsing, style/token
  resolution, key action IDs, center anchor and invalid hierarchy rejection.
- `tools/dev/ui_asset_smoke.mjs`: 10 shared .nui documents, shared UI/2
  style/template checks, in-place hot reload, dynamic text retention, malformed
  document rollback and correction recovery.
- Existing UI root, generic document, UI event, Abby, player camera and First
  Person Protocol tests continue to run without GPU access.
- Targeted syntax compilations for the C++ FPS Player, native UI provider,
  and Qt .nui editor.
- `git diff --check`.

## Remaining limitations

This is not yet a completed production release. Full CMake/Qt Player build,
interactive GPU validation, pointer hit-test screenshot comparison, V8
integration test in a running exported game, and NSP install verification are
still required. Existing low-level engine.ui APIs remain as implementation
interfaces and compatibility shims; the Qt Editor's own chrome remains native
Qt, as appropriate for a desktop editor.

The canvas handles **position dragging**, not yet resize handles, rotated
elements, responsive breakpoint simulation, timeline animation, components,
or a full UI graph editor. Polling script UI reads full bounded .nui files
in development; a future asset-revision API can remove redundant VFS reads.
