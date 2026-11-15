# NorthStar Qt Editor — Details / Properties dark theme

Date: 2026-11-15
Scope: Details | Properties dock in QtEditorWindow.

## Defect
The parent QMainWindow/QDockWidget used the dark theme, but the
QScrollArea viewport and its QWidget form content were left on the
native light palette. This produced a white Details panel and low-contrast
labels while controls remained dark.

## Change
- qt_inspector_scroll.cpp: explicit #151c26 dark QPalette::Window and
  QPalette::Base on the scroll area, named viewport and form content.
  Set autoFillBackground for actual painting on all three surfaces.
- qt_editor_window.cpp: stylesheet selectors for the inspector dock,
  scroll area, viewport, form, labels and checkboxes; matching dark ECS
  Component group, tree and vertical scrollbar.
- qt_inspector_scroll_smoke.cpp: assert object IDs, dark palette,
  actual rendered background pixel, scroll reachability and no
  overlapping Content Browser at three window sizes.

## Verification
- Standalone Qt Widgets + MinGW smoke configured and built under:
  out/inspector-theme-qa-20261115/build
- ctest --output-on-failure: 1/1 PASS including rendered pixel check,
  scroll limits and docking geometry.
- Scoped git diff --check passed for tracked Qt Editor window file.
- Full Qt Editor rebuild and production dist replacement were NOT
  performed as part of this isolated UI fix.
- ABI and project asset formats unchanged.

The test uses Qt offscreen to exercise actual widget rendering, but a
final Windows visual screenshot acceptance pass with the full editor
remains useful before production rollout.
