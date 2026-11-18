# NorthStarCpp — Player/Qt Editor monolith decomposition
Date: 2026-11-16
Build: NorthStarCpp 0.6.2 working tree, independent QA directory `_qa_decomposition`.

## Scope and architecture constraints

This change decomposes two pre-existing orchestration translation units while keeping the original implementation, semantics and pluggable provider ABI unchanged. No Unreal Engine source was copied. The main Win32 Player frame loop, the Qt Editor Play-in-Editor (PIE) lifecycle, input handling, engine.* negotiation, GPU renderer provider selection, ECS and Jolt Physics calls were not edited.

Source context: `window.cpp`, `qt_editor_window_setup.cpp`, and `engine/northstarcpp/CMakeLists.txt` already had extensive local modifications before this operation. `qt_editor_window_setup.cpp` was not tracked by Git at the start; preserve it when staging a future commit.

## New independent translation units

- `modules/editor/window_runtime_options.hpp/.cpp`: `Options` contract, command line validation including Editor embed HWND rules, shared-assets discovery. `window.cpp` uses the original names via small forwarding wrapper/namespace imports.
- `modules/editor/window_runtime_script_contracts.hpp/.cpp`: argument JSON decoding, UI document validation, script indices/numbers and character asset descriptor validation. Original bodies moved unchanged to a separate translation unit.
- `modules/editor/window_runtime_project_files.hpp/.cpp`: VFS script-path safety checks, project root discovery, user-local SaveGameStore namespace, bounded JSON payload formatting. Original code moved, default argument preserved in the declaration.
- `modules/editor/qt_editor_window_content_docks.cpp`: Qt Content Browser tab creation and text-asset opening, retaining the original signal wiring and dock lifecycle.
- `modules/editor/qt_editor_theme.hpp/.cpp`: Qt editor stylesheet, formerly embedded in `setupUi()`.
- `engine/northstarcpp/CMakeLists.txt`: both executables link their respective new translation units.

Reproducible, guarded one-time extraction scripts:
- `tools/dev/decompose_editor_entrypoints_20261116.py`
- `tools/dev/extract_player_script_contracts_20261116.py`
- `tools/dev/extract_player_project_files_20261116.py`

Scripts refuse to run twice when the expected markers are absent and never change the provider ABI. Their extraction was based on the modified current working tree, not an old Git HEAD.

## Quantified decomposition

Measured file sizes (source bytes, not compiler or runtime memory):
- Player `window.cpp`: 309016 -> 287009 bytes, reduction 22007 bytes / ~7.1%.
- Qt `qt_editor_window_setup.cpp`: 114636 -> 105505 bytes, reduction 9131 bytes / ~8.0%.
- Moved lines: 131 Player options, 185 script-contract, 117 script-path/save/payload, 55 Qt content-dock, 148 Qt stylesheet; about 636 original lines (including comments/blank lines). New file sizes include their own includes and namespace declarations.
- No measured FPS gain, RAM reduction, or per-frame allocation reduction is claimed from this refactor.

## Build verification

Previous `_qa_ui_audio` directory had been available earlier in this operation, then disappeared from the workspace. A separate QA directory was configured rather than writing to production dist:
```
cmake -S . -B _qa_decomposition -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build _qa_decomposition --target northstarcpp_player northstarcpp_editor_qt --parallel 4
cmake --build _qa_decomposition --target northstarcpp_geometry_external northstarcpp_geometry_invalid --parallel 4
```
All listed builds completed with exit code 0, including separate compilation and linking of the new source units. No distribution replacement was performed. After these tests, the temporary `_qa_decomposition` build directory was also removed from the workspace by a concurrent process; it must be regenerated for further test runs. Source files remained present. At a later status check the repository HEAD was `e6be4509` and the new source files were already Git-tracked, consistent with concurrent repository activity; this operation did not itself run `git commit` or `git push`.

## Selected runtime/regression tests

In `_qa_decomposition`:
- PASS Qt Editor authoring, native UI, GPU surface
- PASS Player provider inspection (built-in, compatible external override, incompatible ABI rejection)
- PASS Player bundled scripting
- PASS Player scene runtime, prefab runtime
- PASS Qt Editor starts Player
- PASS Player scene validation and invalid-scene rejection
- Qt mouse-look: failed once (RMB+W produced zero movement), then passed on isolated retry. Treat this as *intermittent*, not comprehensively stable.
- FAIL `northstarcpp_player_pistol_runtime`: `Pistol runtime collider is missing`; also failed in earlier `_qa_ui_audio` directory before the final extraction. At the start of this work, `Projects/NorthStarFirstPersonPlatform/prefabs/weapon/pistol.nprefab`, `modules/host/physics.cpp`, and `modules/physics/jolt/jolt.cpp` were already modified, and they were not changed by this refactor. Root cause and pre-refactor baseline are unverified; do not characterize it as conclusively unrelated or newly introduced.

Earlier in `_qa_ui_audio`, selected Qt mouse, native UI, authoring, GPU, provider, scene validation, scene runtime, prefab and Qt launch tests passed. No full engine regression suite, clean-room release package, or extended PIE/HUD GPU acceptance performed.

## Next phase

- Break `QtEditorWindow::setupUi()` into behavior-focused member methods in separate source files after identifying local-variable dependencies and signal ownership; preserve exactly-once signal hookup.
- Split Player App lifecycle into named modules (input and Win32 dispatch, runtime bootstrap, scene bootstrap, render loop, PIE handshake) using an explicit composition root without duplicating timers or frame loops.
- Add scoped lifetime/event ownership tests, allocation profiling and clean build/runtime acceptance.
- Reconcile the pre-existing pistol-collider failure, determine whether input mouse-look smoke is timing-sensitive and run long-lived GPU acceptance before release.
- Stage only intentional source and QA changes; no automatic commit/push and no production `dist` update.
