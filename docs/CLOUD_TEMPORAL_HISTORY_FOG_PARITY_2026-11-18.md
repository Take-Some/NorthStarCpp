# NorthStarCpp — Cloud temporal infrastructure, Vulkan per-fragment fog, GPU QA
Date: 2026-11-18
Status: PARTIAL. The new cloud-temporal shaders and reference logic compile, but
**the GPU history render passes are NOT yet connected** to either renderer.

## Completed: Vulkan per-fragment height fog
- Replaced the earlier interpolation of fog opacity in `ColoredVertex.color.a`.
  Opaque world vertices now retain alpha=1.
- Both `basic.vert/basic.frag` and `world.vert/world.frag` transmit the real
  world position per fragment; the shader computes the fog optical depth
  using camera position, fragment height and travel distance.
- Both Vulkan forward PSOs bind a 32-byte fragment push constant:
  camera XYZ, fog density, and a sky-daylight-derived haze tint.
  This is independent of deferred rendering.
- Updated `renderer_device.cpp`, `renderer_surfaces.cpp`,
  `renderer_frame.cpp` plus the checked-in
  `shader_spirv.hpp/material_spirv.hpp` arrays using
  `tools/dev/embed_vulkan_forward_fog_shaders.py`.
- The Vulkan renderer DLL and GPU smoke (present, geometry, resize, readback)
  built and passed.

## Completed: cloud-only temporal reference & GPU shader sources
- Native `render/shared/sky_cloud_temporal.hpp` includes a weather-aware
  `SkyCloudHistoryTracker`, spherical cloud-shell reprojection independent
  of scene far clipping, and `resolve_cloud_temporal`.
- Reference rejects stale frames on first frame, camera cut/teleport,
  window resize, weather coverage discontinuity, cloud-enable toggle,
  large sun-angle changes, cloud opacity/depth mismatch.
- The reference clamps previous RGB to current 3x3 neighborhood variance
  and scales history weight with camera motion.
- `tests/sky_cloud_temporal_smoke.cpp` compiled and passed.
- GPU shader sources:
  `resources/sharedAssets/shaders/sky/temporal_cloud_resolve.hlsl` and
  `temporal_cloud_resolve.glsl`. Both compile to DXBC/SPIR-V, respectively,
  via `tests/sky_cloud_temporal_shader_compile_smoke.py`.
- Registered reference and shader smoke tests with engine CMake.

## Completed: real GPU pixel comparison
- Extended `tests/sky_gpu_dynamics_smoke.cpp` with `--save-sky`.
  No synthetic images: PPM files contain actual framebuffer readback.
- Vulkan and D3D12/WARP GPU QA: PASS with `--gpu-uniforms --save-sky`.
- Output: `out/qa/sky_frames/` (19 captured sky images per provider;
  12 cloud-motion camera poses).
- Analysis script: `tools/qa/compare_sky_gpu_captures.py`
  writes `sky_parity_report.json`.
- Cross-backend mean absolute RGB difference: noon 11.827/255,
  noon-overcast 16.577/255, overcast 4.035/255,
  first moving-cloud pose 17.246/255, final pose 17.445/255.
- Median successive-frame unregistered RGB8 MAD:
  Vulkan .364, D3D12 .428.
  With approximate integer horizontal yaw-shift alignment:
  Vulkan .300, D3D12 .275.
- These are captured-frame differences, **not** perceptual quality scores,
  GPU-matched linear HDR differences or evidence of true temporal AA.

## Remaining mandatory GPU integration
1. Allocate sky-only current RGBA16F color+opacity and first-hit cloud-depth
   textures, plus two ping-pong history sets, per provider.
2. Split cloud raymarch into a cloud-only pass. Preserve the existing
   Rayleigh/Mie atmosphere independently; do not copy scene+HUD into
   cloud history.
3. Add Render Graph passes: Cloud Trace -> Cloud Temporal Resolve ->
   Sky/Atmosphere Composite. Manage Vulkan image layout barriers and D3D12
   transition/RTV/SRV lifetimes.
4. Pass current/previous camera basis, position and projection uniforms
   and mark resets on resize/teleport, sun/coverage cuts and shader reload.
5. Store cloud opacity and physically meaningful first-hit depth so
   disocclusion rejection uses actual geometric/optical evidence.
6. Verify pixel readback after temporal resolve (cloud ghosting, moving
   camera, panning horizon, dynamic weather) and track p95/p99 GPU time.
7. Accept Vulkan/D3D12 parity by matching scene linear lighting and exposure,
   not comparing unrelated HDR/SDR transfer functions.

## Build notes / no false claims
- Vulkan DLL rebuilt and `northstarcpp_render_vulkan_smoke.exe` passed.
- D3D12/WARP and Vulkan Sky GPU captures passed.
- Standalone cloud temporal C++ and both GPU-shader compilation tests passed.
- CMake/Ninja regeneration encountered an unrelated permission-denied
  recompaction during this turn. The cloud temporal test and extended GPU
  capture harness were compiled directly via MinGW instead, without removing
  user workspace files.
- Production `dist` was not replaced; no new Player rebuild was performed
  after the latest source changes. No commit/push.
- GPU cloud history/reprojection/accumulation is **not live** in the game yet.
