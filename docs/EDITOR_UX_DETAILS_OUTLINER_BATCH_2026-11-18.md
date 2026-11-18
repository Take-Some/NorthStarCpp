# NorthStarCpp Qt Editor — Details / Outliner / Batch / Shortcuts / Drag & Drop

Date: 2026-11-18

## Scope

Second native Qt editor usability pass, taking workflow ideas from UE without copying or linking Unreal Engine code. The original editor Scene V3, QActions, QSettings, and editScene undo/redo contracts remain authoritative.

### Details

- Preserve the existing category headers, per-category expansion persistence, field controls, and scrollable inspector.
- Details search now accepts whitespace-separated AND terms, negated terms (`-word`), category scope (`group:Transform`), and field scope (`field:Mass`).
- Feedback label `detailsSearchFeedback` reports matched field count and clearly signals an empty result.
- Searching does not commit pending property edits. Existing multi-select mixed-value and tri-state handling remains intact.

### World Outliner / batch editing

- Context menu `Transform > Copy Primary Transform` writes a versioned `NorthStar.ActorTransform.v1` JSON payload to the clipboard.
- `Transform > Paste Transform to Selection` validates every numeric component and authored transform range before modifying any actor.
- Paste affects the current stable-ID selection and is applied in a single `editScene` transaction, producing one undo step.
- Existing rename, duplicate, multi-delete, visibility, layer, descendants, and attach-to-parent context actions remain available.
- Fixed `editor_tools::delete_subtree` to remove ECS and opaque records owned by deleted scene entities (including descendants), retaining unrelated and scene-global records. Previously stale records made `validate_scene` reject batch deletion.

### Keyboard Shortcuts

- The existing QAction-backed Keyboard Shortcuts editor continues to persist overrides in QSettings.
- Conflict detection now rejects both exact duplicates and ambiguous prefix sequences, e.g. an assigned `Ctrl+Shift+P` conflicts with `Ctrl+Shift+P, Ctrl+1`.
- No duplicate shortcut dispatcher was introduced.

### Drag & Drop

- Viewport drag hover identifies placement (actors, mesh, prefab) versus target assignment (material or texture).
- Placement feedback shows projected ground XYZ, while material/texture feedback tells the author to drop on an actor.
- Unsupported asset classes are rejected at drag-move, with no false placement cue.
- Drag feedback uses a transient QLabel and GPU-safe tooltip without modifying the scene until drop.

## Build & tests

- `ninja -C out/build northstarcpp_editor_qt`: PASS (exit 0)
- `QT_QPA_PLATFORM=offscreen out/build/northstarcpp_editor_qt.exe --smoke`: PASS
- `QT_QPA_PLATFORM=offscreen out/build/northstarcpp_editor_qt.exe --place-actors-smoke`: PASS
- Added smoke checks for category/field-scoped Details queries and empty-result feedback, shortcut prefix collision, and material drop cue; the prior actor drag/undo and multi-selection checks remain.
- Linker still reports existing Boost duplicate-section diagnostics; Ninja occasionally reports build-file recovery warnings, but the successful editor build returned 0.
- Running interactive GUI visual QA and full D3D12/Vulkan framebuffer comparison is NOT claimed; offscreen Qt cannot validate those properties.

## Files

- `engine/northstarcpp/modules/editor/qt_editor_window_inspector_workspace.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window_workspace_signals.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_keyboard_shortcuts.hpp`
- `engine/northstarcpp/modules/editor/qt_editor_viewport.cpp`
- `engine/northstarcpp/modules/editor/editor_tools.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window_smoke.cpp`

The workspace was already heavily modified by other work at the beginning of this pass. This update does not commit or push the repository, replace production dist, or alter user scenes.
