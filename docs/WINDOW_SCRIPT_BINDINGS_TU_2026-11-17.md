# NorthStarCpp — compiled ScriptRuntime binding units

Date: 2026-11-17
Workspace: C:\Users\Kayla\Documents\NorthStar\NorthStarCpp

## Implemented

The implementation of App::configure_script_bindings(ScriptRuntime&) was removed from the App class body and is now independently compiled in engine/northstarcpp/modules/editor/window_script_bindings.cpp. window.cpp retains WinMain and includes a shared private App definition from window_app_internal.hpp. CMake builds distinct WinMain, binding, and save-binding translation units.

Source files:

- engine/northstarcpp/modules/editor/window.cpp — WinMain
- engine/northstarcpp/modules/editor/window_app_internal.hpp — shared private App class, inline methods and runtime state
- engine/northstarcpp/modules/editor/window_script_bindings.cpp — the single out-of-class App::configure_script_bindings definition
- engine/northstarcpp/modules/editor/window_script_bindings.hpp — internal typed capability contract and forward declarations
- engine/northstarcpp/modules/editor/window_script_save_bindings.cpp — independent engine.save API registrar
- tests/window_script_bindings_modularity_smoke.py — static boundary regression
- tests/window_script_save_bindings_e2e.py — real runtime hardware regression
- tools/dev/extract_window_script_bindings_tu.py — one-shot guarded migration with original-byte backups under out/refactor_backups/

During extraction: window.cpp became 14,658 bytes; window_app_internal.hpp was 36,631 bytes; the primary separate binder was 105,071 bytes before the save split.

Named internal namespace and inline helper functions/constants were used so both independently compiled units refer to one C++ App type; no public ScriptRuntime API or engine.* contract changed.

## Scoped App state-access contract

ScriptSaveBindingsContext now holds one capability: std::function<SaveGameStore()> open_current_project_store. App supplies it via a callback that invokes project_save_store(config,file_path) **on each call**, preserving save-as semantics.

window_script_save_bindings.cpp registers engine.save.write/read/exists/remove and does not include the complete App definition. Existing validation messages, JSON encoding and return semantics were preserved. The old included binder contained 108 runtime.bind calls; the new core plus save bindings also contain 108 (104+4).

**Important architecture limitation:** The primary App binding TU still includes the full private App class header and directly accesses other App fields. This is the first *real narrow capability boundary* (project storage), not the completion of a full PIMPL or 108-callback interface migration. Next extract Scene, Camera, ECS, Input, Player and Audio capabilities incrementally.

Legacy window_runtime_script_bindings.inc remains present but is not compiled or included. It changed concurrently after initial migration (a sky logging line was added). That logging addition is also present in the new core binding source. Do not overwrite the concurrent legacy file without coordination.

## Build and binary verification

Immediately after initial independent binder extraction, standard CMake Player target build succeeded: both source objects compiled and northstarcpp_player.exe linked.

Following the narrower save split, ordinary CMake builds were blocked by Windows file-in-use errors, not C++ compilation failures: WinError 5 when replacing archive/northstar_shared.nsp, and ld Permission denied when replacing northstarcpp_player.exe.

The three affected C++ object files were then compiled independently with the *exact configured Ninja compiler commands*. An independent link using the existing configured arguments, changing only output executable/import-library paths, returned code 0 and created:

out/build/northstarcpp_player_script_bindings_qa.exe

No pre-existing Player binary, running process, archive or production dist was forcibly replaced.

Exact strong-definition inspection via nm -C:

| Object | App::configure_script_bindings | bind_project_save_api |
| --- | ---: | ---: |
| window.cpp.obj | 0 | 0 |
| window_script_bindings.cpp.obj | 1 | 0 |
| window_script_save_bindings.cpp.obj | 0 | 1 |

The original MinGW/Boost RTTI section warnings remain.

## Actual gameplay regression

Command:

    python tests/abby_viewmodel_profile.py out/build/northstarcpp_player_script_bindings_qa.exe --view fps --backend d3d12 --frames 1000 --out out/profile_script_binding_facade_20261117 --timeout 115 --native-autostop --gpu

Result: PASS; exit code 0; 1,000 completed game frames, 1,000 GPU timestamp samples; hardware NVIDIA Quadro RTX 4000.

- Raw frame interval p95: 39.32709 ms
- CPU wall p95: 37.76332 ms
- GPU frame p95: 5.49660 ms

This is a *functional regression profile*, not a controlled FPS uplift comparison.

Save API runtime test:

    python tests/window_script_save_bindings_e2e.py out/build/northstarcpp_player_script_bindings_qa.exe

Result PASS. Native V8 executed engine.save.write/read/exists/remove, checked the JSON round-trip, deleted a temporary slot and persisted an independent success marker inside isolated temporary LOCALAPPDATA. The test temporarily copies NorthStarFirstPersonPlatform, never writes into the source project, and is not registered in default headless CTest because it requires working graphics hardware.

## Test results and outstanding blockers

After capability extraction, 9/9 focused CTest passed: project gameplay scripts, script-service override, scripting ABI, viewport draw modularity, App binding modularity, FPS weapon effects, script framework, script import registry and engine API modularity.

A previous broader selective CTest completed 17/22. Three test fixtures used obsolete project/scripts paths; those have been updated to canonical assets/scripts locations.

Three WARP-dependent integration tests still fail early with Windows 0xC0000005: player_scriptless_root, player_scene_runtime and player_pistol_runtime. The early fault also reproduces with unmodified NorthStarFirstPersonPlatform/main.nscn using --warp --fps, so it is not solely an injected test-script failure. Root-cause stack trace unavailable. D3D12 hardware runs succeed. Embedded engine API catalog test is also failing in the broader suite and requires its own investigation.

**Full CMake final replacement and complete project-wide CTest have not been certified.** No commit/push.

## Next steps

1. Extract narrower scoped capabilities for Scene/Camera/ECS/Input/Player/Audio while preserving script-event and object lifetimes.
2. Diagnose the WARP software-provider crash with a native stack trace; do not weaken test assertions to force a pass.
3. Unblock concurrently used build/archive outputs; rerun ordinary full Release CMake and complete serial CTest.
4. Rebenchmark on equivalent scenes and hardware after further changes before claiming FPS improvements.
