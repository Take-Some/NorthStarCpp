# NorthStarCpp Qt Editor — RMB-activated scene flight

Status: implemented, built and regression-tested in developer Release build.
Date: 2026-11-15

## Expected operation

- **Hold RMB** in scene Viewport to activate free-flight camera mouse look.
- With RMB held: **W/S** forward/back, **A/D** left/right, **Q/E** down/up.
- **Shift** increases travel speed; **Ctrl** is optional fine-speed mode.
- Moving mouse while RMB held controls yaw and pitch.
- Release RMB: flight inputs, inertia, and timer stop immediately.
- Without RMB, WASD does not move the scene camera; Qt Editor selection
  and other editor shortcuts may continue normally.
- Orbit Studio keeps its separate LMB/RMB orbit camera; it is not changed.

The source implementation lives exclusively in first-level modules:
`engine/northstarcpp/modules/editor/qt_editor_viewport.cpp/.hpp`.
Flight is tied to RMB press/release, independent of Ctrl.
`QEvent::ShortcutOverride` captures standard fly-navigation keys ahead
of Qt menu shortcuts while RMB is held. Windows physical keyboard
scan-code mapping preserves WASD/QE under non-Latin layouts.
GPU child surface forwards the same input including shortcut override.
Focus-out and Escape stop flight; no global hotkey hooks required.

The Editor's existing `--mouse-look-smoke` test was extended to send
real Qt mouse/key events and verify:
- WASD without RMB does not move the camera.
- Right-click enables plain WASD/QE with no modifier.
- All six movement directions are correct.
- RMB release prevents further motion even if a key was held.
- Qt ShortcutOverride is captured only during RMB flight.
- Physical Windows scan code for W works even with Qt::Key_unknown.
- Existing pitch/yaw, orbit centre, and passive-hover acceptance preserved.

## Verification

`out/project-manager-qt/northstarcpp_editor_qt.exe` — rebuilt successfully.

CTest `northstarcpp_qt_editor_mouse_look`,
`northstarcpp_editor_camera_picking` and
`northstarcpp_content_browser_qt`: **3/3 PASS**.

The RMB flight GUI acceptance was repeated six consecutive times:
**6/6 PASS**.

`git diff --check`: PASS.

During rebuilding two independent, pre-existing Qt build blockers
were also corrected: `using fs=std::filesystem;` was changed to
`namespace fs=std::filesystem;` in the YMAT library editor,
and local `slots` was renamed to `usedSlots` in the material asset
dialog to avoid Qt's signal/slot macro expansion.

No production `dist` replacement, commit, or push performed.
