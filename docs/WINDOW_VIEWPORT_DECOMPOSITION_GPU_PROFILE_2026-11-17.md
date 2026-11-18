# NorthStarCpp — Win32/Viewport decomposition and native CPU/GPU profile

**Date:** 2026-11-17
**Repository:** C:\Users\Kayla\Documents\NorthStar\NorthStarCpp
**Configuration:** CMake Release / Ninja / MinGW GCC, canonical `out/build`.
**Scope:** Source-level extraction of stateful Win32 and Viewport modules, build/test repair, GPU-capable per-frame profiler and matched-scene device measurements.

## 1. Source decomposition and ABI stability

Extracted original source bytes, without control-flow modifications, into included implementation fragments. Both original files remain their original compilation units, preserving access to `App` and `Viewport::State`, lexical semantics, and externally visible ABI.

| Source | Original bytes | After bytes | Reduction |
| --- | ---: | ---: | ---: |
| `engine/northstarcpp/modules/editor/window.cpp` | 245,252 | 50,176 | 79.5% |
| `engine/northstarcpp/modules/viewport/viewport.cpp` | 200,390 | 49,992 | 75.1% |

`window.cpp` fragments:

- `window_runtime_fps_bridge.inc`: 18,083 bytes
- `window_editor_workspace.inc`: 60,442 bytes
- `window_runtime_script_bindings.inc`: 95,074 bytes
- `window_runtime_script_lifecycle.inc`: 21,654 bytes

`viewport.cpp` fragments:

- `viewport_state.inc`: 42,703 bytes
- `viewport_draw.inc`: 107,756 bytes

Reproducible extraction: `python tools/dev/decompose_runtime_sources.py --plan` (before extraction), `--apply` (once only), `--verify` (after extraction). An exact-byte reconstruction check passed during extraction. Original file backups:

- `out/refactor_backups/window_editor_dac1d151d243.original`
- `out/refactor_backups/viewport_viewport_2c917ef154db.original`

**Limitation:** This is a lexical split into `.inc` files, not yet isolated `.cpp` translation units / PIMPL. It does **not** by itself reduce total compiled code, build time, memory use or runtime frame time. Next extraction should establish proper internal service state and compiled interfaces; a genuine independent renderer submission implementation requires moving accessors across a private API.

## 2. CMake and compiler gate

The initial full build was blocked by CMake tests referencing missing `northstarcpp_audio_winmm` targets. Replaced three incompatible-provider negative-test references with existing `northstarcpp_audio_northstar`, gated WinMM-specific tests with `if(TARGET ...)`, and updated `tests/capability_smoke.cmake` fixture copy from the obsolete WINMM variable to the real AUDIO target. Added missing `<algorithm>` in `tests/fps_weapon_effects_smoke.cpp`.

During subsequent concurrent source changes, a fresh compilation of `modules/host/scripting.cpp` exposed a missing declaration for `quote_json(std::string_view)` before its use in import lowering; a forward declaration resolves that compile error.

After these changes:
```powershell
cmake --preset release
cmake --build out/build --parallel 4
```
**PASS, exit code 0**, including `northstarcpp_player.exe`, Qt Editor, render providers, tools and test executables. Full build was repeated after native frame-profiler changes and again passed. No production `dist` replacement, commit or push.

Nonfatal warnings still observed for MinGW duplicate Boost RTTI sections and `Viewport::State` internal-linkage warning.

## 3. Native frame profiler

Modified:
- `engine/northstarcpp/modules/shared/frame_profiler.hpp`: opt-in native GPU timestamp samples, validity flag, per-frame GPU p50/p95/p99 and sample count in machine-readable frame report; CPU wall and interval remain distinct.
- `engine/northstarcpp/modules/editor/window_runtime_frame_loop.inc`: opt-in GPU timing after submitted frames and opt-in stop at requested completed engine frames.
- `tests/frame_profiler_smoke.cpp`: GPU sampling smoke.
- `tests/abby_viewmodel_profile.py`: canonical project `assets/scripts/main.js` path and `--native-autostop --gpu` workflow.

QA environment switches (disabled in production unless explicitly requested):

```
NORTHSTARCPP_PROFILE_FRAMES=1000
NORTHSTARCPP_PROFILE_AUTOSTOP=1
NORTHSTARCPP_PROFILE_GPU_TIMESTAMPS=1
NORTHSTARCPP_PROFILE_PARTIAL=1
```

GPU timestamp reads may synchronize device execution and perturb CPU/present timing. Do not treat GPU+CPU numbers as a strict GPU/CPU overlap measurement. The first attempted 1000-frame runs exited before producing a report; this was resolved by native completed-frame autostop. The final D3D12 and Vulkan game profiles both report completed 1000-frame runs.

## 4. Actual AbbyPistolDemo game scene, Quadro RTX 4000

Command:
```powershell
python tests/abby_viewmodel_profile.py out/build/northstarcpp_player.exe --view fps --backend d3d12 --frames 1000 --out out/profile_refactor_20261117 --native-autostop --gpu
python tests/abby_viewmodel_profile.py out/build/northstarcpp_player.exe --view fps --backend vulkan --frames 1000 --out out/profile_refactor_20261117 --native-autostop --gpu
```

Both runs: actual `Projects/AbbyPistolDemo/main.nscn`, V8 scripting, Jolt physics, 1000 native completed frames, hardware GPU. Profile artifacts: `out/profile_refactor_20261117/d3d12_fps_gpu.json` and `vulkan_fps_gpu.json`, accompanied by complete runtime logs.

| Metric (ms) | D3D12 | Vulkan |
| --- | ---: | ---: |
| Valid GPU samples | 1000 | 999 |
| Frame interval p50 | 28.63970 | 30.51340 |
| Frame interval p95 | 31.93684 | 245.06727 |
| Frame interval p99 | 46.40093 | 274.48287 |
| CPU frame wall p50 | 17.14705 | 13.76570 |
| CPU frame wall p95 | 22.22366 | 244.15965 |
| CPU frame wall p99 | 31.47162 | 273.56477 |
| Rendering CPU stage p50 | 10.72220 | 12.04500 |
| Rendering CPU stage p95 | 12.85952 | 239.30810 |
| Rendering CPU stage p99 | 23.03888 | 263.71596 |
| GPU timestamp p50 | 3.53854 | 4.47453 |
| GPU timestamp p95 | 4.62994 | 9.87626 |
| GPU timestamp p99 | 6.38048 | 11.66997 |
| Scripting CPU p95 | 2.37767 | 3.81951 |
| Animation CPU p95 | 1.12040 | 1.51164 |
| Physics CPU p95 | 0.68583 | 1.49954 |

Render-stage **instrumented memory/copy operations over entire 1000-frame run**:

| Counter | D3D12 | Vulkan |
| --- | ---: | ---: |
| Allocation events | 2,077 | 24,612 |
| Allocated bytes | 281,498,080 | 15,231,604,292 |
| Geometry copied bytes | 10,600,775,424 | 20,571,601,968 |
| Texture copied bytes | 296,599,552 | 19,342,870,680 |
| Palette copied bytes | 0 | 61,646,848 |
| Device allocation events | 18 | 3 |

**Important:** memory totals above are cumulative *instrumented* operations, not peak memory, peak VRAM, or unique resident bytes. Extreme startup max frame times include first-frame loading and compilation; inspect p95/p99 and repeat runs with warmup before release claims. Vulkan ~239 ms CPU rendering p95 versus ~9.88 ms GPU p95 is evidence of CPU-side stalls or synchronization, but is not a root-cause proof.

**Next optimization candidates:** Trace frame-level Vulkan `renderer_materials.cpp`, `renderer_skin.cpp`, texture staging/upload and fence/wait events; cache immutable resources and descriptors, update only dynamic transforms/palette, bound per-frame copies and ensure fence-correct lifetime. Reprofile matched FPS/TPS scenes with isolated backend process runs. No controlled *pre-decomposition* matched benchmark was performed; **no claimed FPS uplift**.

## 5. Separate native device fixture (not the game)

Saved `out/profile_refactor_20261117/gpu_d3d12_1000.json` and `gpu_vulkan_1000.json`. Synthetic stress 1000 GPU sample fixture, both hardware RTX 4000:

| GPU probe timing (ms) | D3D12 | Vulkan |
| --- | ---: | ---: |
| GPU p50 | 2.53586 | 0.036112 |
| GPU p95 | 3.21748 | 0.0406128 |
| GPU p99 | 3.35485 | 0.223073 |

This is not frame-time data for the actual game and should **never be compared directly as a renderer speed ranking** against the game samples or as matched-complexity render workloads.

## 6. KaylaNightCity streaming-only offline sample

`northstarcpp_scene_project_profile.exe Projects/KaylaNightCity/main.nscn 1000 out/profile_refactor_20261117/kaylanightcity_scene_cpu_1000.csv`:
- Authored entities 154; HLOD clusters 7; streaming cells 0
- SceneWorld CPU p50/p95/p99 **1/1/1 µs**
- No physics, GPU renderer, full scripting or presentation; **not an FPS estimate**.

## 7. Test gate and remaining failures

10/10 focused tests passed at the end, including script SDK bootstrap composition, engine API factory/modularity, frame profiler, runtime scheduler, Win32 message pump, viewport geometry/builder/scratch, scene cost profiler. The current repo is being concurrently edited, so the SDK bootstrap occasionally becomes stale between CMake regeneration and CTest: regenerate with `python tools/dev/compose_northstar_bootstrap.py --root engine/northstarcpp/resources/sharedAssets/api` and rerun CTest.

A separate `northstarcpp_fps_weapon_effects` CTest **still FAILS** (`Shot did not emit independent smoke particles`). Do not relax its assertion to force green; address genuine VFX behavior or update the test only if the published effects contract has deliberately changed.

Full project-wide CTest has not been rerun to 100% and release deployment is not authorized by this work. No commit/push.
