# NorthStarCpp Qt Editor — modularization and efficiency pass

Date: 2026-11-15
Scope: active C++17/Qt6 Editor in NorthStarCpp, not legacy engine.
Safety: no public engine.* ABI changes were made by this refactor. The user's
running Editor/scene were not terminated or replaced.

## Decomposition

`modules/editor/qt_editor_window.cpp` started at 3,925 lines and is now
approximately 1,056 lines (~73% reduction in this single translation unit).
Methods were moved without changes to public QtEditorWindow declarations:

- `qt_editor_window_setup.cpp`: dock/toolbar/UI construction.
- `qt_editor_window_play.cpp`: Player startup, immersive Play-In-Editor,
  embedding, shutdown, and associated integration probes.
- `qt_editor_window_materials.cpp`: material selection/editing and asynchronous
  preview orchestration.
- `qt_editor_window_smoke.cpp`: editor smoke and GPU smoke regression logic.
- `qt_editor_window_paths.hpp/.cpp`: shared filesystem, Unicode conversion,
  per-scene camera settings, material roots, and extension handling.
- `qt_actor_tree_widget.hpp`: Outliner drag/drop and visibility widget.

The new C++ sources are explicitly registered in the editor's CMake target.
A pre-refactor copy is retained at
`out/refactor_backup/qt_editor_window.cpp` (local ignored build storage).

## Reduced repeated work

- Outliner uses cached QIcons for stable actor types (five categories) and
  visibility states (two states): avoids repeated QPixmap painting for each row.
- Search tokens use simplified whitespace instead of rebuilding a regular
  expression for each refresh; actor UTF-8 fields are decoded once per
  matching attempt, rather than once per free-text term.
- Material choices use a QSet for project-overrides/shared-library deduplication
  rather than repeated linear scans of the material list.
- Async preview results use a per-batch QHash of combobox references to indices,
  preserving the first-match semantics of QComboBox::findData.
- Neutral sphere image and its placeholder icon are cached and reused.
- Old temporary repaint suppression of Outliner was rolled back after it was
  found to interfere with geometry-dependent smoke tests.

These are algorithmic reductions in allocation, repeated decoding and
per-refresh Qt drawing. Frame time, cold/warm loading and memory savings have
NOT yet been quantitatively benchmarked.

## Validation

- `cmake --build out/distribution/gcc-release-ge7m_hyw --target
  northstarcpp_editor_qt --parallel 4` succeeded after extraction.
- An incremental `--target all` build succeeded during the refactor.
- 13/13 selected Qt editor/material/PIE/content browser integration CTests
  passed serially.
- Full CTest snapshot: 222 registered, 203 passed, 17 failed, two skipped.
  This is NOT release-ready; several failures involve absent project fixture
  files, Qt preview/icons, runtime logging and physics.
- Windows GUI `--smoke` depends on Outliner geometry. A QA-only normalization
  of the persisted collapsed dock layout was added. An isolated shadow EXE
  returned exit code 0 on `--smoke`.
- Separate `--gpu-smoke` shadow EXE terminated with access violation
  (`0xC0000005`): GPU runtime acceptance remains BLOCKED.

During QA a user-owned instance of `northstarcpp_editor_qt.exe` was running
with a FirstPersonPlatform scene. The Windows loader locked that EXE, so the
new objects were linked into
`out/distribution/gcc-release-ge7m_hyw/northstarcpp_editor_qt_refactor.exe`
for validation. The running/editor production EXE was not replaced.

No release QA promotion, Git commit or push was performed.

## Next technical work

1. Diagnose isolated GPU smoke access violation with debugger and call stack;
   verify the crash is unaffected by persisted viewport/editor state.
2. Benchmark 1k/10k Outliner actors and 100/1000 material entries:
   rebuild wall time, allocation counts, p50/p95/p99 UI operation latency.
3. Remove unnecessary duplicated Qt includes from the split translation units
   and evaluate incremental compile times.
4. Continue decomposition of the active Win32 Player/editor monolith
   `modules/editor/window.cpp` with focused regression tests, then partition
   `viewport.cpp` and Project Manager only on uncontended source snapshots.
5. Resolve baseline CTest failures and run fresh QA/cold startup, integrated
   project scene, GPU and package verification before release promotion.
