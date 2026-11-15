# NorthStarCpp Editor — Adaptive Infinite Ground Grid

Date: 2026-11-15
Status: implemented and tested in isolated/developer Release QA.
Production dist not changed.

## Defect

The Qt Proxy Viewport drew a finite XZ grid at world coordinates
`[-100, 100]` in 5m increments. Flying away from the world origin left
the authoring plane without a grid. Its single-resolution lines were also
too coarse when zoomed in and increasingly dense at distant perspectives.
The native GPU viewport did not honor the editor's Show Grid option.

## Implementation

A reusable, provider-independent ground-grid algorithm was added at:

- `engine/northstarcpp/modules/viewport/editor_grid.hpp`

It computes a bounded visible neighbourhood around the camera's
ground-facing ray and continuously rebuilds it as the camera flies.
This makes the working grid effectively unbounded in the editor rather
than a finite map object. The generated line count remains capped,
independent of scene size or the camera's world coordinates.

The algorithm uses:
- Dynamic camera-relative XZ origin and footprint.
- 1/2/5 logarithmic minor-step selection scaled by projected metres/pixel.
- Major divisions every 5 minor intervals.
- World X (red) and Z (blue) origin axes, separate minor and major colors.
- No ECS/physics objects or scene serialization side effects.
- Finite-value and geometry-budget guards for far-away and invalid cameras.

### Qt Proxy Viewport

The old fixed-coordinate loop was replaced with the new grid layout.
The Qt painter applies antialiasing, varying minor/major/axis line widths,
viewport clipping, and near-plane segment clipping. This avoids huge
coordinates and invalid line projection when the ground plane passes
near the camera's horizon.

### Native GPU Viewport

The Qt Editor passes Show Grid and viewport pixel dimensions through the
existing internal `ViewportGizmo` overlay state. The existing Viewport
facade uses the **same grid layout** and appends transient thin axis-aligned
GPU strips above the XZ ground. They are separate from the ECS scene,
do not affect collisions, and are never enabled for ordinary FPS game
rendering. No `engine.render` plugin ABI changes were necessary.

The Show Grid checkbox now invalidates the GPU frame as well as Qt paint.

## Verification

New native test:
- `northstarcpp_editor_grid`: **PASS**. Checks distant camera
  coordinates (~85km), camera-height-dependent LOD, world origin axes,
  bounded line counts, horizon orientation, invalid input rejection.

Extended Qt viewport test:
- `northstarcpp_qt_editor_gizmo`: **PASS**, including real screenshot
  pixel differences with Grid on/off at near and distant camera positions,
  in addition to Move/Rotate/Scale gizmo tests.

Full developer suite in
`out/ymat-entry-editor-qa-20261115`:
- `northstarcpp_editor_grid`: PASS
- `northstarcpp_qt_editor_gizmo`: PASS
- `northstarcpp_qt_editor_mouse_look`: PASS
- `northstarcpp_qt_editor_authoring`: PASS
- `northstarcpp_qt_editor_gpu_surface`: PASS

Total: **5/5 PASS**.

Since the ordinary `northstarcpp_editor_qt.exe` was temporarily locked by
a running process during a relink, the latest objects were also linked
under a separate name in the same QA build:

`out/ymat-entry-editor-qa-20261115/northstarcpp_editor_grid_qa.exe`

Actual executable smoke:
- `--mouse-look-smoke`: PASS
- `--smoke`: PASS
- `--gpu-smoke`: PASS

The independent QA executable leaves the running editor untouched.
No production dist replacement or git commit/push was performed.

## Remaining improvement

The GPU implementation uses lightweight transient render geometry through
the existing renderer, not a dedicated analytic infinite-grid fragment
shader. Subpixel temporal anti-aliasing and physically depth-faded grid
shading could improve the appearance further on very oblique horizons.
