# NorthStarCpp Renderer — ZF/RAGE Renderer Migration

Date: 2026-11-14

## Input and integration rule

Input: user-supplied `renderer(3).zip` (renderer/ tree, 290 archive entries).
The external renderer includes GBuffer, DeferredLighting, TiledLighting,
RenderPhases, Shadow cascades, occlusion and PostProcessFX systems.
The original C++ classes depend on RAGE-specific device, shader, draw-list and
memory interfaces unavailable in NorthStarCpp.

NorthStarCpp therefore implements the transferable *rendering architecture*
using its existing C++ runtime, materials and stable `engine.render` plugin
ABI. Do not link the original game-specific translation units directly.
Do not rename existing engine subsystems or add a separate compatibility runtime.

## Implemented in current source tree

### 1. Native D3D12 material G-buffer

`engine/northstarcpp/modules/render/d3d12/d3d12.cpp`

- One geometry pass writes three MRT textures:
  - RT0 RGBA8_UNORM — linear albedo + material coverage.
  - RT1 RGBA16_FLOAT — world normal + signed roughness (unlit flag).
  - RT2 RGBA16_FLOAT — emissive RGB + metallic.
- A typeless R32 depth allocation has a typed D32_FLOAT DSV and R32_FLOAT
  shader resource view. It is **not** copied into another texture.
- Fullscreen D3D12 lighting resolves using GGX, a directional light, material
  emissive, normal mapping and inverse View-Projection depth reconstruction.
- Resources are allocated lazily when authored material batches are present,
  recreated after swapchain resize, and reused across stable frames.
- D3D12 barriers handle SRV -> RTV -> SRV and DepthWrite -> SRV -> DepthWrite.
- Sky, primitive triangles, debug grid and HUD retain their original
  rendering support; opaque material depths still test against earlier geometry.

### 2. Scene FXAA

`engine/northstarcpp/modules/render/shared/postfx_hlsl.hpp`

- Native fullscreen FXAA renders from an offscreen scene-color texture
  into the swapchain.
- The 3D scene (sky, opaque geometry and deferred lit materials) is filtered
  **before** rendering the first-person HUD. UI stays sharp.
- `NORTHSTARCPP_DISABLE_FXAA=1` disables this optional pass at renderer
  construction for visual A/B acceptance and profiling.
- Scene-color target is reused between frames and rebuilt on resize.

### 3. Backend-independent Render Graph primitives

`engine/northstarcpp/modules/render/shared/render_graph.hpp`

- Pass read/write sets and declared dependencies.
- Validation of transient read-before-write.
- CPU pass scheduling, resource lifetimes and hazard reporting.
- Important: the GPU backend is still **manually recorded** in D3D12.
  This is not a complete backend-neutral GPU Render Graph executor.

### 4. GPU timestamps / profiling

`modules/api/include/northstarcpp/render_timing_api.h`

- A non-breaking optional `NorthStarRenderTimingApi` extension exposes
  GPU timestamp frequency, frame GPU time and per-stage times:
  opaque, deferred, postfx and HUD. The D3D12 provider records five timestamp
  query points and resolves them into a small readback buffer.
- Explicit `read()` waits for the most recently submitted GPU frame.
  Production should query sparingly to avoid changing the frame cadence.
- WARP and physical D3D12 adapters passed the timestamp ABI tests.
- Lit material test measurements (single frame, 440x330 resize fixture):
  - Hardware GPU: 0.1220 ms total; deferred 0.0919 ms; postfx 0.0301 ms.
  - WARP: 16.3260 ms total; deferred 6.8502 ms; postfx 9.4741 ms.
- These are diagnostic **single-frame** results, not stable percentiles,
  normalized hardware benchmarks or per-frame editor statistics.

### 5. Compatibility

- `NORTHSTARCPP_RENDER_ABI` remains **5**; material extension stays
  compatible with existing engine.runtime and Qt Editor callers.
- D3D12 provider metadata advanced to **1.3.0**.
- D3D11 authored materials continue using forward GGX; not yet migrated to
  deferred MRT. Vulkan is not part of this change.
- No built production `dist` or installed plugin was overwritten.

## Validation

Windows CMake/Ninja build: `northstarcpp_render_d3d12`,
`northstarcpp_render_d3d11`, shared material smoke, native D3D12 smoke,
Render Graph smoke and deferred inverse-matrix smoke — built successfully.

Tests:
- `northstarcpp_d3d12_material_warp`: pass, includes UV ownership, lit
  GGX/PBR, 64 repeated frames, window resize and real GPU backbuffer readback.
- `northstarcpp_d3d11_material_warp`: pass; same material regression.
- `northstarcpp_d3d12_warp`: pass; geometry/sky/HUD/readback/resize.
- `northstarcpp_d3d12_warp_stress`: pass at 1000 frames. CPU+Present
  (WARP, vsync) p50 16.656 ms, p95 17.262 ms, p99 33.023 ms.
- `northstarcpp_d3d12_material_hardware`: pass (physical GPU).
- `northstarcpp_d3d12_hardware`: pass (physical GPU).
- GPU timestamp extension negotiation and per-pass sum: pass on WARP and hardware.
- Lit material GPU timestamps after resize: pass on WARP and hardware.
- `northstarcpp_render_graph`: pass.
- `northstarcpp_deferred_math`: pass.
- FXAA disabled: both D3D12 WARP material and scene readback tests pass.

These validate actual D3D12 command execution under WARP and the
hardware D3D12 tests above. The WARP CPU+Present measurements include vsync
and are **not GPU frame timings**; they do not establish native-GPU
p50/p95/p99 performance or visual parity with the source renderer.

## Source component status

| Source archive component | NorthStarCpp status | Next acceptance |
| --- | --- | --- |
| Deferred/GBuffer | D3D12 material G-buffer implemented | Cover all opaque scene geometry and D3D11/Vulkan |
| Deferred/DeferredLighting | Directional GGX resolve implemented | Runtime point/spot lights, IBL, light volumes |
| Lights/TiledLighting | Not migrated | Frustum/depth tile classification, GPU light lists, budget |
| RenderPhases | Backend-independent CPU planner exists | Execute real passes with transient resource allocation |
| Shadows/CascadeShadows | Not migrated | Cascaded shadow maps + stabilized splits + PCF |
| occlusion / occlusionAsync | Not migrated | Depth pyramid/Hi-Z, conservative tests and readback latency |
| PostProcessFX: anti-aliasing | FXAA implemented on D3D12 | TAA, jitter, motion vectors, ghosting validation |
| PostProcessFX: HDR/bloom/exposure | Not migrated | HDR scene format, adaptation, tonemapping and bloom |
| AdaptiveDOF | Not migrated | Depth-aware CoC and bokeh with quality presets |
| Mirrors / Reflections | Not migrated | Reflection captures / screen-space reflections |
| LensArtefacts | Not migrated | HDR lens effects separated from HUD |
| Tiled light shadows | Not migrated | Material/opaque shadow compositing |
| Multi-queue async compute | Not migrated | Profile before enabling |

## Next safe implementation sequence

1. Extend the ECS-to-render extraction to emit real material/mesh surfaces,
   dynamic lights and camera exposure using replaceable engine.render services.
2. Make Render Graph execute pass descriptors on D3D12 with explicit resource
   transitions, aliased transient attachments, immutable frame allocations and
   per-frame fences; retain the current backend as rollback path.
3. Implement GPU tiled/clustered light culling and point/spot/directional PBR,
   then shadow map integration using a measured per-light budget.
4. Build a depth pyramid, conservative Hi-Z occlusion and temporal controls
   with deterministic stress tests for geometry popping.
5. Introduce HDR targets, tonemapping, bloom and SSAO; finally TAA/DOF.
6. Confirm hardware GPU timestamps plus >=1000 valid frame samples with
   p50/p95/p99, cold/warm residency and editor/game visual equivalence.

## Operating notes

Set `NORTHSTARCPP_DISABLE_FXAA=1` before starting the Player or Qt Editor
to disable the effect in the D3D12 renderer. Unset it to use the default FXAA.

No production installation was replaced and no commit/push was requested
or performed. Respect all pre-existing uncommitted project modifications.


## 2026-11-14 continuation — tiled dynamic lighting and opaque G-buffer

### Dynamic lights (NorthStar D3D12 provider 1.4.0)

- The `NorthStarRenderLightsApi` extension is an optional, versioned
  `engine.render` module query, preserving the existing render ABI 5.
- `NorthStarRenderLight` is a copied 48-byte POD record: xyz/range,
  linear RGB/intensity, direction/cosine; negative cosine selects point light.
- Maximum 256 dynamic lights. Values validated before replacing the provider
  list; zero count clears it.
- Backend-neutral CPU tile classification computes conservative screen-space
  light lists in 32x32 tiles. Max 64 candidate indices per tile; overflow
  explicitly marks the tile for all-light traversal instead of silently
  discarding lights.
- D3D12 uploads the light structs and index arrays into persistently mapped
  upload buffers, each exposed through an SRV. Deferred lighting evaluates
  the tile's candidate point/spot lights with attenuated GGX contributions.
- The editor/player Viewport reads version-1 `engine.render.light` scene
  components, resolves optional entity-relative positions, and invokes the
  replaceable provider extension. Providers without the extension are
  warned once and continue rendering, not crashed.
- Scene component payload contract: ASCII numbers
  `x y z range r g b intensity dx dy dz coneCos`. Values are checked
  for finite coordinates, nonnegative light intensity, positive finite
  radius, and valid spotlight direction. This is a minimal scene authoring
  contract, not a full editor light gizmo or an ECS light controller.

### All opaque geometry in shared D3D12 deferred path

- Primitive color geometry (cubes and triangle stream) now writes the
  three G-buffer targets using an additional D3D12 MRT PSO. Surface normals
  come from pixel derivatives of the interpolated world-space triangle.
- Material geometry shares the same MRTs and depth. The lighting resolve
  is invoked once, after both primitive and authored material geometry.
- Skydome and debug grid remain forward, UI/reticle remains post-FXAA.
- Alpha-blended material graph batches stay on their separate forward
  path. This is not yet full deferred support for all material types.

### SSAO prototype (optional)

- `NORTHSTARCPP_ENABLE_SSAO=1` opts into an 8-tap view-space occlusion
  approximation from the depth buffer in the deferred-lighting shader.
- Ambient-only effect with world-space radius/bias; default disabled.
- This is **not** a separate denoised half-resolution SSAO pass,
  GTA/ZF screen-space AO parity, or measured production-quality feature.

### Acceptance and remaining work

- D3D12 DLL, runtime static library, and Viewport static library compiled.
- CMake CPU tiled-light and scene-component validation passed.
- Material PBR, GPU geometry, HUD, resize and hardware/WARP D3D12 tests passed.
- 1000-frame D3D12 WARP stress test passed after opaque geometry migration.
- Optional SSAO enabled: four focused tests passed.
- No hardware p50/p95/p99 profiling for a populated multi-light scene.
  No quantified light-count scaling or visual reference comparison.
- The Render Graph still plans CPU dependencies; **the D3D12 command list
  is not yet executed by the graph**. Transient aliasing and multi-queue
  scheduling are not implemented.
- Remaining: true GPU tiled/clustered classification at scale, light shadow
  maps and cascades, Hi-Z/occlusion, denoised SSAO, HDR/bloom/exposure,
  TAA and history reprojection.
- Existing production dist not modified; no commit or push performed.

### File additions

- `modules/api/include/northstarcpp/render_lights_api.h`
- `modules/render/shared/tiled_lights.hpp`
- `modules/render/shared/render_light_components.hpp`
- `tests/render_tiled_lights_smoke.cpp`

The source archive `renderer(3).zip` was inspected for its Deferred,
TiledLighting and GBuffer architecture; its RAGE-specific translation
units are not linked into NorthStarCpp.

## 2026-11-14 continuation — Bloom and first directional shadow map (D3D12 1.5.0)

Reference review: `renderer(3).zip` contains `PostProcessFX.h/.cpp`,
`RenderPhaseCascadeShadows.cpp`, `ShadowTypes.h`, and
`ParaboloidShadows`. The source renderer exposes exposure curves,
bloom threshold/intensity, filmic tonemapping, DOF, and PCSS settings
through its shadow system. These subsystems are **not linked** into
NorthStarCpp: this implementation is engine-native and uses the
existing versioned `engine.render` plugin ABI.

### Implemented and integrated

1. **Bloom prefilter + composite** (`postfx_bloom_hlsl.hpp`,
   `postfx_hlsl.hpp`, `d3d12.cpp`):
   - `NORTHSTARCPP_ENABLE_BLOOM=1` enables an extra fullscreen bright-pass
     into a quarter-resolution `R16G16B16A16_FLOAT` texture.
   - Its filtered highlights are composited with the D3D12 FXAA output.
   - Scene and bloom targets are recreated on resize; state transitions are
     explicit. The shader bypasses FXAA if `NORTHSTARCPP_DISABLE_FXAA=1`.
   - The original scene surface is still **LDR RGBA8**. This is LDR bloom,
     **not** a complete HDR exposure/tonemapping pipeline.

2. **One directional shadow map with 3×3 PCF**
   (`shadow_hlsl.hpp`, `deferred_hlsl.hpp`, `d3d12.cpp`):
   - `NORTHSTARCPP_ENABLE_SHADOWS=1` enables depth-only rendering
     of opaque color geometry and opaque UV/PBR meshes from a directional
     light using a camera-centered orthographic light projection.
   - A typeless R32 depth texture has D32 DSV and R32_FLOAT SRV. Direct
     directional GGX lighting samples its depth through 3×3 percentage
     closer filtering. Ambient and emissive contributions remain unshadowed.
   - Hardware shadow map resolution: 1024×1024; software WARP uses a
     256×256 QA map to reduce test cost.
   - Shadow map state transitions `PIXEL_SHADER_RESOURCE` ->
     `DEPTH_WRITE` -> `PIXEL_SHADER_RESOURCE` are explicit. An early
     depth-only PSO initialization failure was fixed by clearing unused
     RTV formats and write masks.

3. **PCSS-style penumbra widening** (`deferred_hlsl.hpp`):
   - `NORTHSTARCPP_ENABLE_PCSS=1` (requires shadows enabled) runs an
     eight-sample blocker search and grows the nine-tap PCF radius based
     on the receiver/blocker depth separation.
   - This is a lightweight, opt-in *approximation*, not the complete
     NVIDIA PCSS library or ZF cascade-specific implementation.

4. **Corrected postprocess ordering** (`d3d12.cpp`):
   - Forward-blended translucent material graph surfaces are composed
     before bloom and FXAA, and while the scene depth target is bound.
   - HUD is still rendered after postprocessing and is not blurred.

### Validation performed

- Source compiled as native D3D12 1.5.0 with MinGW C++17, then linked
  into an **isolated QA DLL** at
  `out/zf_postfx_shadow_verified_qa.dll`. This avoids altering or
  unloading production modules while other Ninja builds have active
  handles on the shared build directory.
- With bloom, shadows and PCSS all enabled:
  - Hardware D3D12 scene/skydome/HUD/resize/readback acceptance: PASS.
  - Hardware D3D12 lit PBR/UV material acceptance: PASS.
  - WARP D3D12 scene/skydome/HUD/resize/readback acceptance: PASS.
  - WARP D3D12 PBR/UV material acceptance (64 repeated frames): PASS.
- Direct tests use the same existing smoke-test executables, supplied
  explicitly with the isolated QA DLL path. One earlier CTest run with
  unoptimized WARP shadow size hit its 45-second timeout; follow-up
  direct QA passed after reducing WARP map resolution, **not** by
  claiming the timed-out run succeeded.
- Isolated CMake configuration at `out/zf_postfx_cmake_qa`: PASS.
  Two new CTest names were verified as registered:
  `northstarcpp_d3d12_postfx_shadow_warp` and
  `northstarcpp_d3d12_postfx_shadow_hardware`.
  Those isolated-build CTest entries were **not** executed there because
  that directory has not built all test executables. Their equivalent
  existing executables were run directly as described above.

### Limitations / next stage

- Current shadow map covers one fixed ±80-unit region around the camera.
  No stabilized camera cascades, adaptive cascade splits, cross-cascade
  blending, distant shadow residency or material alpha-mask casting.
- No local point/spot shadow maps, cubemaps or paraboloid shadow passes.
- Bloom currently uses one quarter-resolution bright-pass, not multi-mip
  gaussian bloom or lens artefact pipelines.
- No HDR linear scene target, exposure histogram, ACES/filmic tonemap,
  temporally reprojected TAA, Hi-Z occlusion, GTAO/denoised SSAO, or
  framegraph-driven GPU pass execution.
- Hardware sample GPU times reported by the smoke tests are snapshots;
  do not interpret them as >1000-frame p50/p95/p99 profiling or
  production performance guarantees.
- No production `dist` replacement, git commit or push was performed.


### Final follow-up QA: independent Bloom / FXAA switching

The optional Bloom+FXAA-off path initially failed a D3D12 hardware
readback. The actual bug was a stale conditional selecting the backbuffer
as the scene render target whenever FXAA was disabled, even if Bloom was
enabled. The D3D12 provider now selects the intermediate Scene Color
target through `postfx_enabled()`, not `fxaa_enabled` alone.

When Bloom is enabled and FXAA is explicitly disabled, the postprocess
path uses `CopyResource` to transfer unfiltered Scene Color to the
backbuffer, followed by a dedicated additive Bloom overlay PSO. The
copy resource transitions are reversed before presentation. This keeps
the HUD separate and preserves sky color.

`NORTHSTARCPP_DISABLE_FXAA` is now honored only when its value is
exactly `1`; the value `0` keeps FXAA enabled.

The final isolated QA DLL is:
`out/zf_postfx_shadow_verified_qa.dll`.

On that exact build:
- Hardware D3D12, `NORTHSTARCPP_DISABLE_FXAA=0`, Bloom on: PASS.
- Hardware D3D12, Bloom on, FXAA off, shadows on: PASS.
- Hardware D3D12, Bloom + shadows + PCSS + FXAA: PASS.
- WARP D3D12 scene, Bloom on, FXAA off: PASS.
- WARP D3D12 material PBR acceptance with Bloom + shadows + PCSS
  (including 64 repeated frames): PASS.

The other earlier hardware/WARP scene and material checks completed
successfully. Running future renderer benchmarks on a game scene with
real geometry, CSM, alpha-masked vegetation and moving lights is still
required before making any 60/120 FPS performance claim.
