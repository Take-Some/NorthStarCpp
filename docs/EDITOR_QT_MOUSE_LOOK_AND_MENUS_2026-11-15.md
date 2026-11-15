# NorthStarCpp Qt Editor — Pointer Camera Direction and Menu Hover

Date: 2026-11-15
Status: implemented, tested in developer Release build. Production dist not promoted.

## Camera mouse direction

Fixed inconsistent Qt mouse delta mapping for free-fly and orbit views.

Previous `QtEditorViewport::mouseMoveEvent` handed Qt deltas directly to
`EditorCamera::look(dx, -dy)`. However, `EditorCamera::look` already
negated its Y input to update pitch. Moving the pointer upward consequently
lowered the camera pitch.

Added `EditorCamera::drag_look(pointer_dx, pointer_dy, sensitivity)`
to explicitly distinguish editor pointer drag from camera/world-space look.
It calls `look(-pointer_dx, pointer_dy, sensitivity)`, making the authored
viewport drag in the expected direction without changing game camera
semantics, scripted look, WASD movement or wheel zoom. The same function is
called for RMB fly look and LMB/RMB orbit look.

Changed source:
- `engine/northstarcpp/modules/editor/editor_camera.hpp`
- `engine/northstarcpp/modules/editor/editor_camera.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_viewport.cpp`
- `tests/editor_camera_smoke.cpp`

Added `northstarcpp_editor_qt --mouse-look-smoke`, which sends real Qt 6
mouse button/move/release events into `sceneViewport`:
- RMB drag left/up changes yaw and pitch in expected authored orientation;
- no camera rotation on passive hover without button;
- LMB orbit drag left/up changes yaw and pitch consistently;
- target position remains fixed throughout orbit;
- screen projection, opposite drag, pitch limits, invalid inputs are
  independently covered by provider-neutral EditorCamera smoke tests.

Qt test uses nondeprecated QMouseEvent constructors and real global
pointer coordinates.

## Menu hover

Previous editor QSS styled QMenuBar and QMenu base colors, without
`QMenuBar::item:selected` / `QMenu::item:selected` for highlighted
mouse-hover items.

Added reusable theme file:
- `engine/northstarcpp/modules/editor/qt_menu_theme.hpp`

Applied at the QApplication level in:
- `qt_editor_main.cpp` (dark)
- `qt_asset_browser_main.cpp` (dark)
- `qt_project_browser.cpp` (light)
- `camera_timeline_editor.cpp` (light)

The Qt Editor window-local conflicting base-menu QSS rule was removed.
Common style includes selectable hover/pressed colors, disabled actions,
submenus and separators without changing unrelated widget styles.

Added `northstarcpp_qt_menu_hover` test that moves the pointer and
inspects *actual rendered Qt menu pixels* in both themes, including
context menu rows, top-level menubar, nested submenu triggers/actions,
and stylesheet precedence.

## Verified

`out/menu-hover-qa-20261115`:
- `northstarcpp_qt_menu_hover`: PASS (Windows Qt real pixel check).

`out/project-manager-qt`:
- `northstarcpp_editor_qt` built and linked (Release) PASS.
- `northstarcpp_editor_camera_smoke` built and linked PASS.
- `northstarcpp_qt_editor_mouse_look`: PASS.
- `northstarcpp_editor_camera_picking`: PASS.
- `northstarcpp_content_browser_qt`: PASS.
- All four Qt application entry points syntax-compiled without errors.
- Project Browser, Content Browser and Camera Timeline Editor exe
  compilation/link targets were built in the development output.

Separate general QA tests from the older build are not all green:
`northstarcpp_qt_editor_authoring` fails an independent material
assignment fixture; `northstarcpp_project_manager_window` returns 8;
the project catalog smoke executable is missing in that build directory.
These are not suppressed or represented as camera/menu regressions.

Nothing deployed to production `dist` and no Git commit/push performed.
