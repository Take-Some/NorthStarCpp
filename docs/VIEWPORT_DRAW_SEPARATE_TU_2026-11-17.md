# NorthStarCpp — independently compiled Viewport::draw

Date: 2026-11-17

## Objective and implemented boundary

The original stateful `Viewport::draw` implementation is no longer textually included in `viewport.cpp`. It is compiled independently as:

- `engine/northstarcpp/modules/viewport/viewport_draw.cpp` — the single out-of-class `Viewport::draw` definition and its draw-specific helpers
- `engine/northstarcpp/modules/viewport/viewport_internal.hpp` — shared private definitions and helper declarations, with `viewport_state.inc` included once per translation unit
- `engine/northstarcpp/modules/viewport/viewport.cpp` — retains the other `Viewport` methods and shared internal header
- `engine/northstarcpp/CMakeLists.txt` — explicitly adds `modules/viewport/viewport_draw.cpp` to `northstarcpp_viewport`.

The private provider/character helper namespace is now named `viewport_detail`, allowing source-level definitions to be shared without anonymous-namespace type mismatches. The public `viewport.hpp` interface was not altered by this extraction.

Source sizes as measured during extraction:

| Source | Size |
| --- | ---: |
| `viewport.cpp`, before this extraction | 49,992 bytes |
| `viewport.cpp`, after extraction | 41,339 bytes |
| New `viewport_draw.cpp` | 108,875 bytes |
| New `viewport_internal.hpp` | 7,907 bytes |
| Shared private `viewport_state.inc` | 42,703 bytes |

The original input `viewport.cpp` was preserved in:

`out/refactor_backups/viewport_pre_compiled_draw_9abf0f603631.original`

The extraction script `tools/dev/compile_viewport_draw_unit.py` is a **one-shot migration script**: it refuses to overwrite an already split source. Do not run it repeatedly on the new module layout.

## Native build and symbol proof

Executed:

```powershell
cmake --build out/build --target northstarcpp_viewport --parallel 4
cmake --build out/build --parallel 4
```

Both returned exit code 0. The full Release build linked `northstarcpp_player.exe`, `northstarcpp_editor_qt.exe`, supporting tools and test executables.

Build objects confirmed separately:

- `viewport.cpp.obj` — 537,580 bytes
- `viewport_draw.cpp.obj` — 1,340,338 bytes

GNU `nm -C --defined-only out/build/lib/libviewport.a`, filtered to the global `T` symbol for `northstarcpp::Viewport::draw(`, reported **exactly one exported definition**.

Existing nonfatal MinGW/Boost RTTI section mismatch warnings remain. No ABI comparison against a separate clean baseline binary was performed, but the public header and function declaration were unchanged.

## Regression verification

Added static structural regression test:

`tests/viewport_draw_modularity_smoke.py`

Registered in CMake as `northstarcpp_viewport_draw_modularity`. It rejects reintroducing a duplicate draw implementation or legacy `.inc` inclusion and verifies the shared private state is used and the independent TU is listed in CMake.

- Focused Viewport, FPS effects, runtime scheduler, frame profiler and engine API suite: **20/20 passed**.
- `northstarcpp_viewport_draw_modularity`: **1/1 passed**.

After extraction the full `AbbyPistolDemo` Vulkan 1000-frame profile was run:

```powershell
python tests/abby_viewmodel_profile.py out/build/northstarcpp_player.exe --view fps --backend vulkan --frames 1000 --out out/profile_viewport_split_20261117 --timeout 110 --native-autostop --gpu --vulkan-diag
```

Runtime result: PASS; 1000 engine frames and 999 valid GPU timestamp samples. Quadro RTX 4000 hardware renderer. Measurements:

| Metric | p50 | p95 | p99 |
| --- | ---: | ---: | ---: |
| Raw frame interval | 30.51325 ms | 32.09810 ms | 49.23612 ms |
| CPU frame wall | 13.40150 ms | 23.45528 ms | 48.20646 ms |
| CPU rendering | 11.66945 ms | 18.64980 ms | 35.39196 ms |
| GPU frame | 3.80880 ms | 9.61400 ms | 11.73188 ms |

Vulkan diagnostics: frame-fence wait p95 0.0106 ms; image acquire p95 0.012205 ms; 143 texture uploads and 5 descriptor rebuilds. These are single-run measurements, with GPU timestamp instrumentation enabled. They cannot establish a before/after FPS gain attributable to the C++ extraction, particularly with unrelated concurrent edits in the repository.

## Full CTest status (not green)

An attempted full-suite parallel CTest run recorded 41 failed cases in `Testing/Temporary/LastTestsFailed.log`, including unrelated stale fixtures. At least one failure was confirmed when run individually: `northstarcpp_engine_layout` expects a missing legacy `audio/winmm` module directory. The isolated `northstarcpp_fps_player` test also failed and needs investigation. This is **not** a completed green full regression gate. The targeted `northstarcpp_fps_weapon_effects` did pass in the focused test run.

Do not deploy/replace the production `dist` or claim complete release certification based on this stage. No commit or push performed.

## Next extraction boundary: Win32 ScriptRuntime

`window_runtime_script_bindings.inc` still contains `App::configure_script_bindings` with many lambdas capturing `this` and private scene, audio, ECS, camera, and player state. Moving those lambdas into an independent `.cpp` without a shared `App` definition would not compile. It was intentionally **not** changed in this stage.

Next create a narrow internal script-binding host/state contract, move `configure_script_bindings` to an out-of-class method in a dedicated compiled `window_script_bindings.cpp`, and require native build, script-lifecycle tests and gameplay reprofile before removing the `.inc`. Avoid a broad public ABI change or unsafe manual duplication of callbacks.
