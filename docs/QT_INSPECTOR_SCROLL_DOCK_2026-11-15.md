# Qt Editor — Scrollable Details | Properties Dock
Date: 2026-11-15

## User-facing behavior

The Details | Properties dock now respects the geometry allocated to it
by Qt's main-window dock splitters. If the selected actor's properties,
material picker, transform fields, ECS Components or help text do not fit
vertically, the panel scrolls independently instead of covering the
Content Browser below.

- One outer `QScrollArea` owns all inspector content and uses
  `setWidgetResizable(true)` with a vertical scrollbar as needed.
- The dock and its scroll widget have zero hard minimum height.
  The form's content is allowed its intrinsic minimum size *inside*
  the scroll area rather than imposing it on `QDockWidget`.
- `QFormLayout::WrapLongRows` wraps field rows in a narrow Inspector.
- ECS Components remains a bounded 144-pixel tree with its own
  table controls, embedded in the vertically scrollable form.
- The Inspector is movable between left and right dock areas but is
  not floatable above other editor panels; historical floating
  workspace states are re-docked on restore.
- All existing Inspector names, property bindings, material assignment,
  ECS record editing and Apply Object Properties callbacks are preserved.

## Source changes

- `engine/northstarcpp/modules/editor/qt_inspector_scroll.hpp`
- `engine/northstarcpp/modules/editor/qt_inspector_scroll.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_main.cpp`
- `engine/northstarcpp/CMakeLists.txt`
- `tests/qt_inspector_scroll_smoke.cpp`

The Editor and the small independent QA harness both use the exact
same `createInspectorScroll()` Qt helper.

## Verification

Build directory: `out/inspector-dock-qa-20261115`.

- CMake/Ninja configuration: PASS.
- `northstarcpp_inspector_scroll_smoke` standalone Qt executable: BUILT.
- `northstarcpp_inspector_scroll_components` CTest: PASS.
- Test covers 1150×720, 900×460 and 780×390 window sizes, verifies
  that Details and ContentBrowser dock rectangles do not intersect,
  asserts vertical scroll range, reaches the Apply button and bottom
  help/ECS section, and checks dock minimum-size constraints.
- Full Editor `qt_editor_window.cpp` syntax-only compilation: PASS.
- Editor entrypoint `qt_editor_main.cpp` syntax-only compilation: PASS.
- `git diff --check`: 0.
- Full integrated UI smoke (`--inspector-layout-smoke`) is registered
  but not executed against the final Editor executable in this run.
  A fresh full Editor build pulls hundreds of runtime dependencies;
  parallel compilation hit Windows paging-file limits. The isolated
  Qt harness and C++ syntax checks passed instead.

## Deployment

This change is local to NorthStarCpp source. No production `dist`
binary was overwritten and no Git commit or push was performed.
