# NorthStarCpp 0.6.2 — Frame Loop, Viewport and Qt Project Browser Optimization

Date: 2026-11-17.
Scope: local, incremental optimization of existing source; **not** a production release, and not a replacement of concurrent changes.

## 1. Frame loop / scripting UI event path

Files:
- `engine/northstarcpp/modules/editor/window.cpp`
- `engine/northstarcpp/modules/editor/window_runtime_frame_loop.inc`

The Player instance now retains a `std::string ui_event_payload_scratch` as per-instance working storage for emitted UI event JSON, including bool/number/string/null values. The frame loop clears and reuses it for subsequent events instead of constructing a fresh string from chained `operator+` expressions. The serialized JSON keys, encoding and event ordering remain unchanged. Other UI event temporaries (e.g., quoted individual strings) are still present; this is not a zero-allocation serializer. The existing frame profiler is left opt-in and unchanged.

The monolithic App is still larger than is desirable. This patch does not claim a new independently compiled FrameLoop library or improved aggregate FPS.

## 2. Viewport mesh residency

Files:
- `engine/northstarcpp/modules/viewport/viewport_mesh_residency.hpp` (new)
- `engine/northstarcpp/modules/viewport/viewport.cpp`
- `tests/viewport_mesh_residency_smoke.cpp` (new)
- `engine/northstarcpp/CMakeLists.txt` (registers regression CTest)

The earlier per-draw `unordered_set<string>` copied asset paths and allocated a node for each visible mesh. The replacement `ActiveMeshRefs` borrows `string_view` values from the Scene for the duration of the synchronous draw, using persistent flat open-addressed hash slots and generation tags. Assets with file extensions .obj/.ydd/.ydr (including selectors after @) retain the same matching semantics. The caller clears borrowed names after mesh eviction, and the state clears them on project reset. Only high-water buffer growth allocates; hash entries do not allocate individually.

A first experimental sorted-vector version passed correctness tests but was 2–7% slower than the baseline. It was replaced with this faster open-addressing design and was NOT retained.

**Synthetic CPU benchmark** (GCC 16.2, Release/O2, 10,000 asset paths, 90 passes):

| Run | Original unordered_set<string> | New ActiveMeshRefs | Speedup |
| --- | ---: | ---: | ---: |
| 1 | 135.494 ms | 25.404 ms | 5.33x |
| 2 | 137.106 ms | 24.976 ms | 5.49x |
| 3 | 135.796 ms | 24.254 ms | 5.60x |
| 4 | 140.679 ms | 24.225 ms | 5.81x |

The benchmark checks **799,920 identical lookup results**. This is a local CPU mesh-residency workload, NOT an end-to-end frame-time or FPS speedup. The no-copy map also reduces allocator churn; no heap profiler has yet quantified bytes saved in live gameplay.

## 3. Qt Project Browser

File: `engine/northstarcpp/modules/project_manager/qt_project_browser.cpp`

- Filtering and sorting now uses `vector<const Project*>` borrowed from the loaded catalog instead of copying each Project (including strings, tags, filesystem paths and provider maps).
- Search fields and tag conversions are now skipped when the query is empty; tag matching short-circuits.
- Category/favorite/recent navigation filters the in-memory catalog without redundant `Catalog::list()` and provider rediscovery. The explicit Refresh button continues to reload disk state.
- ResizeEvent requests are coalesced into one scheduled callback; cards are recreated only if the grid's 1–4 column count changes.
- Navigation tabs retain mutually exclusive selection state.

Qt Project Manager native `--smoke` exit 0.

## 4. Compile and integration acceptance

Independent Release build:
`out/qa/_optimization_physics_release_build`

- `northstarcpp_player`, `northstarcpp_editor_qt`, `northstarcpp_project_manager`, `northstarcpp_viewport`: **PASS**
- Selected CTest after building target executables: `northstarcpp_runtime_frame_scheduler`, `northstarcpp_viewport_frame_builder`, `northstarcpp_viewport_frame_scratch`, `northstarcpp_viewport_mesh_residency`, `northstarcpp_jolt_residency` — **5/5 PASS**
- Qt Project Browser `--smoke`: **PASS**
- Qt Editor `--smoke`: **PASS**
- Qt Editor `--gpu-smoke`: **PASS**
- Newly built Player `--check-scene`: AbbyPistolDemo and KaylaNightCity both `READY`, exit 0, 0 failures

The entire >286-test suite was **not** re-run for this change set. Previous known failures are not waived.

## 5. Real CPU frame timeline baseline

Built-in test scene without project scripts, D3D12 Player + Jolt provider, `NORTHSTARCPP_PROFILE_FRAMES=1000`. Captured actual 1,000-frame profiler output.

| Metric | p50 | p95 | p99 |
| --- | ---: | ---: | ---: |
| Raw frame interval (ms) | 29.9844 | 34.2044 | 47.0748 |
| Frame CPU wall (ms) | 0.3758 | 2.2269 | 3.1601 |
| Rendering stage CPU wall (ms) | 0.2854 | 1.6533 | 2.7360 |
| Scene streaming CPU wall (ms) | 0.0056 | 0.0196 | 0.0236 |

Raw intervals include scheduling, waiting and presentation; they are not identical to CPU utilization. The profile recorded 1 instrumented animation allocation of 32 bytes, and zeros for the other *instrumented buffer counters*. Those counters are **not** a complete audit of malloc/new, GPU VRAM or transient allocations. Physics stage of the built-in scene had zero work. The profiler explicitly reports `gpu_timestamps=false`: GPU frame and GPU pass times remain **unmeasured**.

Raw log:
`out/qa/optimization_frame_profile_builtin_1000.log`.

## 6. Known blockers and future work

- Frame-profile attempt for AbbyPistolDemo failed before frame collection due to a V8 worker evaluation error: `SyntaxError: Numeric separators are not allowed at the end of numeric literals`. The underlying script/source requires separate investigation. File `out/qa/optimization_frame_profile_abby_1000.log`.
- End-to-end KaylaNightCity FPS comparison requires fixed scene/camera, equivalent visible meshes and vehicle count on baseline and optimized builds, GPU timestamp captures for D3D12/Vulkan and a complete CPU allocation profiler.
- Full code decomposition of `editor/window.cpp` and `viewport.cpp` into separate compiled translation units remains a subsequent high-risk step. This optimization retains existing API and renderer provider contracts.
- The independent build directory had pre-existing CMake object-path length warnings, though the targeted targets successfully linked.
- The working tree contains other concurrent edits. These were preserved, and no commit/push or production `dist` replacement was performed.

## Suggested acceptance gate

1. Resolve V8 project script syntax failure.
2. Capture identical KaylaNightCity scene for 1,000+ frames with repeatable camera/vehicle trajectories.
3. Compare wall p50/p95/p99, GPU timing, allocations, and peak memory between commit baseline and optimized build.
4. Run full CTest and interactive Editor smoke, then publish only after regressions are closed.
