# NorthStar Qt Editor — Unreal Engine 5.3.2 feature-parity audit

Date: 2026-11-15
Workspace: C:/Users/Kayla/Documents/NorthStar/NorthStarCpp
Target: northstarcpp_editor_qt (Qt 6, C++17)
Reference: https://github.com/Mccrae123/Unreal-Engine-5.3.2
Official functional references: https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-interface and https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-level-editor-viewport

## Intellectual-property boundary

This is a feature and workflow comparison, **not** a port of Unreal Engine code. Unreal Engine source is distributed subject to Epic's licensing terms; it cannot be imported into NorthStar's GPLv3 codebase. Implement functionality with NorthStar-owned C++/Qt implementations, public documentation, and independent tests. Do not copy Slate implementations, Unreal headers, assets, shaders, or editor-only modules.

## Current NorthStar implementation (confirmed in active tree)

- Qt 6 QMainWindow with persistent docking, separate Details/Content Browser/Outliner/Log/Providers panels.
- Scene v3/NEF8 authored object hierarchy; stable IDs, layer-aware World Outliner, context actions, search, visibility and drag/drop reparenting.
- Authoring History with validated Undo/Redo, viewport camera controls/bookmarks and translation/rotation/scale gizmos.
- Content Browser integrated with project and shared engine asset roots; native mesh and material preview/edit flows; prefab placement and materials.
- Inspector supports core transforms, materials, physics flag and authored box/sphere/capsule collision shape dimensions/offset; ECS component records are retained and editable as raw payloads.
- Embedded Play surface exists; game runtime still requires integration testing and is not equivalent to full in-process Unreal PIE.
- New in this task: pinned, dark-themed Details property search with case-insensitive row filtering and clear/reset. Filters are UI-only and do not write scene records or mutate Undo history.

## Parity matrix

| Area | Current assessment | Next technical milestone | Acceptance |
| --- | --- | --- | --- |
| Native editor shell | Functional Qt dock shell | Extend layouts, tabbing and keyboard accessibility | Restores layouts, never covers adjacent docks; small-window regressions |
| World Outliner | Hierarchical actor/layer editing and filtering | Multi-actor operations, virtualization, streaming-cell visibility | 10k+ actor stress, p95/p99 selection/rebuild latency |
| Details/Properties | Core properties, collision UI, ECS raw binary editor, new search | Schema-reflected, plugin-registered typed component editors, multi-select | Unknown bytes preserved; typed edits validate; Undo/Redo and serialization roundtrip |
| Level Viewport | Functional camera/gizmo and native GPU bridge in progress | Production Vulkan/D3D12 render graph output, materials, shading, ID-buffer picking | Real assets rendered correctly; resize/hide/show and picking; GPU smoke PASS |
| Collision/physics | Authored primitive shapes and overlay path | Mesh/convex compound colliders, shape transforms, collision channel filters | Save/reload + Jolt/Bullet provider-equivalent tests |
| Assets/Content Browser | Two roots, previews, prefab/material authoring | Async registry/index, virtualized grids, import pipeline and dependency diagnostics | Cold/warm benchmark for 10k/100k entries, no UI stalls |
| Play-In-Editor | Embedded overlay/Player integration path | Separate edit/game state, pause/step/eject and safe stop/restart | Escape restores editor; no leaked state, reliable game frame and assets |
| World Partition | Runtime streaming and scene cells exist | Cell grid/streaming sources, Data Layers, HLOD and residency authoring overlays | Fast travel, collision residency, lazy load and save transaction tests |
| Scene tools | Existing basic placement/parenting/prefabs | Terrain, placement brushes, foliage, splines and actor bulk tools | Cross-sector undo/save, dependency preservation |
| Editor profiling | Previous partial smoke and profiling | p50/p95/p99 interaction/viewport + GPU timestamps, memory | Representative 1k/10k entities and release builds |
| Packaging/QA | CMake/CTest targets and packaging scripts | Fully reproducible release build and deploy verification | All release gates green; no silent production EXE replacement |

## Change set made in this iteration

1. `engine/northstarcpp/modules/editor/qt_inspector_scroll.hpp` — InspectorScrollLayout now returns a container panel and search field.
2. `engine/northstarcpp/modules/editor/qt_inspector_scroll.cpp` — fixed search field above scroll viewport and QLabel/QGroupBox/QAbstractButton-aware row filtering, with Qt version fallback.
3. `engine/northstarcpp/modules/editor/qt_editor_window_setup.cpp` — the Details dock embeds the composed panel, not the scrolling area alone.
4. `engine/northstarcpp/modules/editor/qt_editor_main.cpp` — expanded inspector-layout QA assertions, including filter/reset behavior.
5. `tests/qt_inspector_scroll_smoke.cpp` — new filtering checks in the standalone Qt inspector acceptance test.

## Verification (this session)

- Clean isolated CMake Ninja Release configure: PASS (`out/editor-ue-parity-qa-20261115`).
- Individual GCC C++ compilation: PASS for `qt_inspector_scroll.cpp`, `qt_editor_main.cpp`, `qt_editor_window_setup.cpp`.
- Standalone `northstarcpp_inspector_scroll_smoke` build: PASS.
- Standalone `northstarcpp_inspector_scroll_smoke.exe`: PASS (exit 0), including property filtering, clearing, dark palette, scroll reachability and dock non-overlap at 1150x720, 900x460 and 780x390.
- Full `northstarcpp_editor_qt` build: **BLOCKED**, external V8 15.8.62 archive staging failed with TLS certificate error 77 (cannot download https://storage.googleapis.com/chromium-v8/official/canary/v8-win64-rel-15.8.62.zip). No TLS-check bypass should be introduced. Provision a verified archive or fix the trust store.
- Earlier NorthStar modularization report records `--gpu-smoke` access violation `0xC0000005`; not claimed fixed here.
- No production executable or DLL replaced, no commit or push.

## Immediate priority

P0: Resolve V8 archive provisioning and isolate the Vulkan GPU smoke crash. Once reliable build/gpu gates pass, instrument the viewport and integrate real renderer output / stable actor-ID picking. Parallel P0 usability: extend the typed component metadata schema and Details editors, keeping provider-neutral runtime contracts. Then P1 World Partition overlays and profiling. Feature parity must be measured through editor interaction and runtime QA rather than appearance alone.
