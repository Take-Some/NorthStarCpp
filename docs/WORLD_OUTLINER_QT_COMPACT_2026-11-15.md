# World Outliner — Compact UE-style Qt Panel

Date: 2026-11-15

## Problem

The former World Outliner was a narrow, two-column QTreeWidget with large contrasting row fills and intrusive hover feedback. Actor names became hard to read due to the narrow first column and the redundant Type column. The panel did not provide immediate expand/collapse actions or a visible object count.

## Implemented

- Single-column tree with a hidden header, full-width actor names, 25 px row height, 17 px indentation, native 16 px icons, and no horizontal scrollbar.
- Layer grouping is retained as a folder item; actor parent-child hierarchy and stable NSCN entity IDs remain intact.
- Dark flat colors aligned to the Qt Editor; subtle hover and distinct selection colors.
- Dedicated top search bar with inline clear button and keyboard focus outline.
- Expand All / Collapse All controls.
- Footer shows total actor and layer counts, or filtered actor counts during a search.
- Actor type, entity ID and layer are shown in a tooltip instead of consuming a second table column.
- Existing drag/drop reparenting, context menu (Focus, Rename, Duplicate, Delete, Attach), viewport selection synchronization and ancestor-aware search remain unchanged.
- Smoke assertions added to the existing editor test to protect the compact layout and controls.

## Source

`engine/northstarcpp/modules/editor/qt_editor_window.cpp`

## Validation

The modified QtEditorWindow translation unit compiles with MinGW GCC using the Ninja-generated compiler command; exit code 0. Git diff whitespace check passed.

A full editor executable was **not** produced in this pass: the build is blocked by an unrelated error in `modules/render/d3d11/renderer_device.cpp:59`, where a move-only `Com<ID3D10Blob>` is copied. Consequently, end-to-end editor smoke and live GUI screenshot validation remain pending. No release binary was replaced. No commit/push performed.

This is a compact interface inspired by the Unreal Editor World Outliner, not a claim of complete UE feature parity.
