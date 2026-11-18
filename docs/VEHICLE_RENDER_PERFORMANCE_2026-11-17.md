# NorthStarCpp — Vehicle FPS and MT28 rendering optimization

Date: 2026-11-17

## Constraints
All original texture pixels, source-resolution YTDs, YDR/YDD mesh vertices,
material references, full-frame render resolution, sampling, and native
Jolt collision quality are preserved. No dynamic resolution, render scale
decrease, proxy replacement, or texture compression/downsampling is used.

## MT28 workload
The project scene starts Mahir MT28 via
`Projects/KaylaNightCity/assets/scripts/main.js`, loads
`vehicles/mahir_mt28.nvehicle` with Jolt `contact_mode=physics`, and
has eight wheels, two doors and many original composite YDR parts.

In project assets, the two MT28 texture directories
`assets/textures/mahir_mt28` and
`assets/textures/vehicles/mahir_mt28`
contain **9 byte-identical files (SHA-256 verified)**, each directory
totalling **158,530,324 bytes**. The 69.7 MiB interior and 47 MiB exterior
YTDs are the largest entries. Paths remain untouched because existing
materials or native archives may refer to either directory. The NSP v1 format
currently requires non-overlapping ascending archive ranges; do not
silently alias paths to the same bytes without a versioned reader change.

## Implemented native optimizations

1. Runtime vehicle physics:
   - For Jolt-native vehicles, do not redundantly issue per-wheel fallback
     sphere sweeps. Jolt VehicleConstraint's own wheel-contact evaluation
     remains authoritative. For non-Jolt fallback vehicles the previous
     full-rate contact sampling and collision sweeps remain.
   - Fast aligned-index comparison for v1 vehicle before/after enumeration;
     gracefully fall back to ID lookup if a replaceable provider changes
     enumeration order.
   - `VehicleService::states_into(vector&)` reuses the caller's existing
     per-frame enumeration storage; `states()` still works unchanged.

2. Runtime visual preparation:
   - Reuse the full-fidelity render-only Scene scratch instead of constructing
     a new temporary Scene each frame. It is reset when project scripts stop.
     Authored scene/physics world are never modified by this scratch.
   - Precompute vehicle chassis yaw sine/cosine and chassis pivot height only
     once per vehicle instead of per wheel/door.
   - Cache the Euler sin/cos values for each composite parent node once per
     frame, reusing the storage for later frames. Apply the exact existing
     transform order; all original part geometry remains present.

3. D3D12 materials:
   - `FrameTextureMemo` reuses a fully validated GPU texture lookup for
     all material batches referencing the same source bytes/dimensions/color
     space **within a single synchronous submission only**.
   - This does not trust memory addresses across frames: changed images
     still pass through the provider's byte-for-byte validation.
   - If all vertices, textures, and material constants are unchanged, skip
     an unnecessary global GPU fence wait. If any content/capacity differs,
     keep the existing GPU synchronization and upload safeguards. This
     benefits parked vehicles / stable scene geometry.

4. Diagnostics:
   - Vehicle advance and visual transforms contribute to FrameProfiler
     physics/animation stages for reliable future p95/p99 investigation.

## Source paths
- `engine/northstarcpp/modules/editor/window.cpp`
- `engine/northstarcpp/modules/editor/window_vehicle_runtime.inc`
- `engine/northstarcpp/modules/editor/window_runtime_frame_loop.inc`
- `engine/northstarcpp/modules/host/vehicle_service.cpp`
- `engine/northstarcpp/modules/api/include/northstarcpp/vehicle_service.hpp`
- `engine/northstarcpp/modules/render/d3d12/renderer_materials.cpp`
- `engine/northstarcpp/modules/render/shared/frame_texture_memo.hpp`
- `tests/render_frame_texture_memo_smoke.cpp`

## Verification
- `cmake --build out/build --target northstarcpp_player northstarcpp_render_d3d12`
- `ctest --test-dir out/build -R "^northstarcpp_(vehicle_|native_vehicle_scene|jolt_vehicle_abi|render_frame_texture_memo)" --output-on-failure`
- `ctest --test-dir out/build -R "^northstarcpp_(d3d12_material_warp|d3d12_texture_pool|material_resources)$" --output-on-failure`

Vehicle/Jolt: 9/9 passes. D3D12 WARP/materials: 4/4 passes (including texture memo).
The Player and D3D12 provider build successfully in the canonical `out/build`.

## Remaining high-cost work
- Real KaylaNightCity FPS A/B capture at identical camera pose, weather and
  screen resolution using CPU/GPU stage timings and 1% lows.
- Replace per-frame world-space material triangle reprojection/upload for
  moving rigid buses with GPU-owned mesh instances/transform buffers
  supported in both D3D12 and Vulkan (while retaining full source vertices).
- Prewarm MT28 mesh/texture residency asynchronously, avoiding render-thread
  stalls when the bus first enters streaming visibility.
- Content-addressed NSP package aliases after format/reader validation, so
  identical MT28 texture paths can share stored bytes safely.
- Profile GPU material submission stalls, including the still-required
  synchronization for geometries that actually move.
- Profile traffic at scale (bus + NPC vehicles), physics p95/p99 and
  GPU frame time; then optimize Jolt broadphase and asset streaming.

A reduction in CPU duplicate work is verified by code/tests, but **a measured
gameplay FPS improvement is not yet claimed**. No commit or push was made.
