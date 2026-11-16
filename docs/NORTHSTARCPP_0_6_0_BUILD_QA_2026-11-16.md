# NorthStarCpp 0.6.0 — Windows Release build / QA status

Date: 2026-11-16 (Europe/Riga local release-day context).
Source tree: `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`
Branch: `main` — HEAD at build time: `dfffc9d6`.
Build configuration: MinGW-w64 x64, GCC C++17, Ninja, CMake Release.
Version: `0.6.0` (Windows FileVersion/ProductVersion `0.6.0.0`).
Source version: `CMakeLists.txt` `project(NorthStarCpp VERSION 0.6.0 ...)`.
README version badge also updated to 0.6.0.

## Build
- Build tree: `C:\Users\Kayla\Documents\NorthStar\cache\northstarcpp-release-ui-20261116`
- `cmake -S <source> -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release`: PASS.
- `cmake --build <build> --parallel 4`: PASS, complete Ninja all-target graph (413 operations).
- Qt Editor, Player, Asset Browser, D3D11/D3D12/Vulkan providers, SDK and QA targets: linked.
- Windows VersionInfo checked on Qt Editor, Player, Asset Browser, D3D12 and Vulkan: `0.6.0.0`.
- `git diff --check`: exit 0 (Git warns some pre-existing line endings will be normalized later).

## Verification
- CTest inventory: 286 tests.
- Broad full-suite attempt: 28 failing tests, so release gate is NOT met.
- Selected critical subset: 16/16 PASS.
- Subset covers Qt Editor native GPU surface, authoring, mouse look, native UI, gizmo, Player Blueprint (including Qt and physics), scene chunks, HLSL/material graph, provider catalog, render features, and editor-to-player launch.

Failure families from CTest re-run:
- Test fixture and project data gaps: `NorthStarEngineQA/qa.manifest.json`, `NorthStarAnimationLab/animations/lift.anim.json`, `NorthStarFirstPersonPlatform/main.nscene`, OBJ sample assets, `prefabs/tree_animate/textures/tree_animate.ytd`.
- Runtime/renderer: `northstarcpp_d3d12_transient_alias_placed`, `northstarcpp_d3d12_pass_graph`, Jolt physics contact and CharacterMotor initialization, game-provider/capability tests.
- Qt/UI authoring: text asset, menu hover, prefab browser/composition, modal preview.
- Runtime scripting, launch and instrumentation: V8 gameplay init/pistol runtime, crash minidump, logger/startup reports.
These must not be disregarded or claimed fixed without a fresh passing full test run.

## Non-shipping QA install
- Installed to `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp\out\qa\NorthStarCpp-0.6.0-windows-x64-Release`.
- `cmake --install <build> --prefix <qa-prefix> --config Release`: PASS.
- Qt6 runtime deployed via the repository's `tools.deploy_qt.deploy()` and `isolate_qt_runtime()` helpers: PASS.
- Installed tree: 345 files, 408,199,737 bytes.
- `bin\qt_tools\northstarcpp_editor_qt.exe`, `bin\northstarcpp_player.exe`, `bin\render_d3d12.dll`: FileVersion 0.6.0.0.
- Windows interactive deployment verified: installed public Qt Editor launcher `--help` returned 0, private Qt Editor `--help` returned 0, Qt Editor `--smoke` returned 0 (PASS), and `--gpu-smoke` returned 0 (PASS). The installed Player `--check-scene --scene Projects/AbbyPistolDemo/main.nscn` returned 0 with startup READY, warnings=0, failures=0, Abby native YDD with 33 model sections loaded.
- Headless-only limitation: installed private Qt Editor tests with `QT_QPA_PLATFORM=offscreen` hung before argument parsing; default Windows QPA operation succeeds. Offscreen deployment behavior remains unresolved. The candidate is runnable in tested standard Windows mode, but NOT approved as an official production distribution due to the 28 CTest failures.

## Delivery restrictions
- Working production distribution and installed default binaries were NOT replaced.
- No release publication, tag, commit or push performed.
- Preexisting unrelated dirty working tree changes were not reverted.
- Next release gate: resolve the 28 CTest failures and any required headless deployment support, rerun all 286 tests, stage with official `tools.build_dist --qa-only` flow, then independently test/approve and promote.
