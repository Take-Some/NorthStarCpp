# NorthStarCpp — Runtime decomposition, Pistol collider and Release QA gate
Date: 2026-11-17
Workspace: C:\Users\Kayla\Documents\NorthStar\NorthStarCpp
Isolated Release build: _qa_reorg_gate_20261117 (Ninja, GCC 16.2, BUILD_TESTING=ON)
No production deployment, version bump, commit or push.

## Completed source/asset changes

1. Pistol collision repaired in `Projects/NorthStarFirstPersonPlatform/prefabs/weapon/pistol.nprefab`:
   the dynamic scene Entity was mass=0.65 but collision=false. Enabled the authored collision bit (0 -> 1) without a weapon-specific engine workaround. `northstarcpp_player_pistol_runtime` passes.
2. Moved CPU procedural geometry staging from `Viewport::draw` into `modules/viewport/viewport_procedural_geometry.hpp/.cpp`: preserves triangle sequence, coordinates, normal/UV policy, picking and validation; adds `viewport_procedural_geometry_smoke.cpp`.
3. Moved the Win32 GetMessage/accelerator/dialog/dispatch loop from `App::run` into separately compiled `modules/editor/window_runtime_message_pump.hpp/.cpp`; adds `win32_message_pump_smoke.cpp`.
4. Fixed mismatched shader-reload key count for in-world UI. `viewport_world_ui_batches.inc` appended each World UI Entity-owned stable key alongside the appended material v1/v2 batches. Without that key the D3D12 provider aborted the first game frame: `D3D12 shader binding count differs from material batches`. `northstarcpp_ui_world_runtime` now passes end-to-end.
5. Root CMake asset dependency glob now filters .bak, .tmp and ~ backup files, consistent with `tools/pack_nsp.py` inventory. Reduces spurious NSP rebuilds but does not eliminate lock contention when legitimate resource inputs change while Player holds the .nsp.
6. Updated `tests/render_d3d12_pass_graph_smoke.cpp` to recognize the registered-but-inactive auxiliary GBuffer MRT, current PostFX graph mode, and renamed scene composite pass. The unchanged renderer graph test now passes.
7. All changes coexist with extensive concurrently edited uncommitted source and assets. No resets or overwrite of other agents' changes.

## Build/test evidence

Fresh Release configuration succeeded. `cmake --build _qa_reorg_gate_20261117 --parallel 4` completed successfully (full build including Qt Editor, Player, GPU providers and all test executables).

Focused regression: 25/25 PASS before later expansion; subsequent focused checks of World UI, scene runtime and Pistol: 5/5 PASS.
New procedural geometry, message pump and Player pistol tests: 3/3 PASS.
Device-level hardware probes: Vulkan and D3D12 PASS on NVIDIA Quadro RTX 4000; GPU readbacks/picking/async readback/timestamps validated independently. D3D12 PostFX GPU readback verified; Vulkan PostFX GPU readback not verified. No pixel-difference cross-backend parity established.

Full 301-case CTest attempted as ordered ranges 1–30,31–80,81–160,161–200,201–250,251–301. Initial aggregate before targeted fixes/retries:
- 271 passed
- 28 failed
- 2 deliberately skipped (D3D12 Enhanced Barriers)
Targeted fixes since those ranges: D3D12 Render Graph (#75) PASS, World UI (#288) PASS; PIE embedded render (#213) passed isolated after an earlier failure under parallel load. Full 301-case suite has NOT been repeated as one passing release gate.

Remaining observed failing groups (not exhaustive root-cause attribution):
- QA fixture absent `Projects/NorthStarEngineQA/qa.manifest.json` (game_distribution, qa_reporter).
- Engine assets/Qt resource lookup for standalone tests, Qt menu hover, NEF8 modal content browser.
- Unresolved reference asset `prefabs/tree_animate/textures/tree_animate.ytd` (unsupported NEF8) and other scene/import tests.
- Player project scripts/startup-report error `Dynamic bodies must use physics` on active NorthStarFirstPersonPlatform scripts, under concurrent project edits.
- Abby skeletal pipeline: weapon ADS rear sight at y=1.505 vs eye y=1.620, expected absolute delta under .045; IK mesh tears logged. Do not adjust test tolerance to make it green without correcting authored rig/hand/weapon transforms.
- Some Jolt, scene-binary, FPS/UI tests, logger and crash reporter still failed in full sequence.
- Embedded PIE controls had false pause/step/resume under -j4 but passed an isolated rerun. Repeatability/concurrency to be investigated.

## Real 1000-frame AbbyPistolDemo profiling

Collected via existing `tests/abby_viewmodel_profile.py` in FPS mode with dedicated source project copies and active V8/Jolt. Machine: NVIDIA Quadro RTX 4000. Results saved in `_qa_reorg_gate_20261117/qa_fps_profiles/{d3d12,vulkan}_fps.json` and logs.

| CPU measure (ms) | D3D12 | Vulkan |
|---|---:|---:|
| raw interval p50/p95/p99 | 30.52535 / 32.18738 / 46.93666 | 30.56870 / 32.08394 / 46.07195 |
| CPU wall p50/p95/p99 | 11.90535 / 21.94791 / 39.98099 | 12.25915 / 17.17230 / 28.79580 |
| rendering CPU p50/p95/p99 | 10.48320 / 18.79956 / 33.76901 | 10.82485 / 14.71596 / 24.31249 |
| instrumented rendering allocation events total | 2,034 | 21,095 |
| instrumented rendering allocated bytes total | 156,477,568 | 5,702,806,920 |
| instrumented rendering geometry copy bytes total | 14,972,544,000 | 18,452,259,360 |

Profiler `gpu_timestamps=false`; these are CPU wall/frame measurements and instrumented buffers only, not total process memory. Median real-game frame interval is ~30.5 ms (~32.8 FPS). No controlled pre-change reference profile, so cannot assert a performance delta.

Independent GPU probe with --stress=1000, not equivalent to the full game scene:
- D3D12: CPU p50/p95/p99 0.1852/1.70248/2.63548 ms; GPU p50/p95/p99 0.778752/3.84796/5.10263 ms.
- Vulkan: CPU p50/p95/p99 16.6649/17.983/20.2234 ms; GPU p50/p95/p99 0.033664/0.038112/0.0694806 ms.
Do not compare probe numbers as apples-to-apples game GPU FPS. Significant steady-state Vulkan memory allocation and repeated geometry copying should be traced to `render/vulkan/renderer_materials.cpp`, `renderer_skin.cpp`, texture and descriptor uploads, with fence-correct lifetime before optimization.

## Outstanding architecture/release requirements

- App still contains large stateful `.inc` fragments. RuntimeServices and AppSimulationState were extracted in earlier phases, but scene/physics/render ownership has not been fully moved to independent .cpp/Pimpl.
- Viewport GPU submission, CPU procedural geometry and frame index now have independent .cpp, but skinned geometry/scene batching is still a large monolith.
- No cross-backend matched-scene pixel parity (D3D12/Vulkan GBuffer, HDR, Bloom, shadows, SSAO, transparent/world UI and skin).
- Full QA is NOT green. Several tests have real or missing-fixture failures despite successful full build and targeted acceptance.
- No production dist replacement until all gating tests, stress and memory issues are reconciled.
