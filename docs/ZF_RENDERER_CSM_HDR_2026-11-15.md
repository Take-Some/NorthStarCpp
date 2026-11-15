# NorthStarCpp D3D12 — CSM/PSSM and HDR Postprocessing Acceptance
Date: 2026-11-15
Provider: `engine.render/d3d12` **1.6.0**
Main render ABI: **5 (unchanged)**

## Scope and integration

The current NorthStarCpp runtime retains its plugin ownership and ABI.
This release extends the existing D3D12 provider and shared shaders; it does
not introduce a second renderer, a legacy compatibility subsystem, or link
the original RAGE/ZF translation units.

## Cascaded Shadow Maps (CSM/PSSM)

### CPU cascade planner

`modules/render/shared/cascaded_shadows.hpp`

- Three view-frustum cascade slices use practical split distribution,
  `split = 0.7 * logarithmic + 0.3 * uniform`.
- The planner derives all eight corners of each slice by inverse view-
  projection, fits a bounding sphere, slightly expands and quantizes
  its radius, then snaps the light-space center to the world-space texel grid.
- A fixed directional light basis and D3D [0,1] depth projection produce
  row-major matrices matching the existing `mul(row, matrix)` convention.
- Each cascade has its own near/far distances, light projection, world
  texel size, and texel-scaled receiver depth bias.
- Matrix/perspective calculations run independently of GPU and plug-in ABI.

### D3D12 shadow map execution

`modules/render/d3d12/d3d12.cpp`

- One `DXGI_FORMAT_R32_TYPELESS` texture array has **3 depth slices**.
- Each slice has a typed D32_FLOAT DSV; the entire texture array has a
  R32_FLOAT Texture2DArray shader view.
- One draw pass per cascade renders camera-visible or *shadow-volume-visible*
  opaque cubes / triangle meshes and opaque material batches.
- Camera-only frustum culling no longer automatically removes cube casters
  that intersect a cascade's light volume.
- GPU barriers handle SRV -> DEPTH_WRITE -> SRV for the complete array.
- One aligned 256-byte per-cascade shadow matrix CBV is used for depth passes.
- On hardware: 1024x1024 per cascade. WARP uses 256x256 per cascade
  for repeatable performance-oriented software tests.

### Deferred lighting

`modules/render/shared/deferred_hlsl.hpp`

- Selects cascade by positive camera-forward depth.
- Blends visibility in the last 8% of a cascade into the next cascade.
- 3x3 PCF available for each cascade.
- Optional 8-tap blocker search with adaptive 3x3 filter radius
  (`NORTHSTARCPP_ENABLE_PCSS=1`) for each cascade.
- Direct directional PBR contribution is shadowed; ambient and emissive
  remain independent.
- Receiver bias depends on cascade texel size and light-space Z scale;
  it is not a universal hard-coded depth value.

## HDR rendering and postprocessing

`NORTHSTARCPP_ENABLE_HDR=1` selects the HDR path:

1. Scene, sky, debug lines, deferred PBR and forward translucent
   Material Graph surfaces render into linear `R16G16B16A16_FLOAT`.
2. Optional bloom bright-pass extracts highlights from the HDR scene
   into a quarter-resolution FP16 texture **before** tone mapping.
3. A separate GPU exposure pass calculates the log average of 64
   evenly stratified scene samples in a 1x1 FP16 texture.
4. Two 1x1 FP16 exposure textures alternate read/write roles, enabling
   a temporal blend of previous and target exposure with 12% per-frame
   adaptation. History is reset on resize. Exposure is clamped to
   0.25–4.0. The graphics queue keeps one frame in flight and uses
   separate descriptor heaps to prevent SRV/RTV overlap.
5. An independent ACES-like filmic tonemapping pass composes the
   linear scene and HDR bloom, applies the adapted exposure and converts
   to sRGB into an RGBA8 intermediate scene target.
6. Optional FXAA resolves SDR scene color to the swapchain, and the HUD
   is rendered *after* tone mapping / anti-aliasing.

When FXAA is disabled while HDR is enabled, the already-tonemapped SDR
scene is copied directly to the backbuffer. Bloom is not double-applied.
The original LDR path remains enabled by default.

### Runtime feature flags

```cmd
set NORTHSTARCPP_ENABLE_SHADOWS=1
set NORTHSTARCPP_ENABLE_PCSS=1
set NORTHSTARCPP_ENABLE_HDR=1
set NORTHSTARCPP_ENABLE_BLOOM=1
```

`NORTHSTARCPP_DISABLE_FXAA=1` disables FXAA without disabling HDR or bloom.
All new features are opt-in; `NORTHSTARCPP_RENDER_ABI` remains 5.

## Validation

The sources were built in an isolated CMake/Ninja Release directory:

`out/zf_postfx_cmake_qa`

The following eleven selected CTest cases passed in a single run:

- `northstarcpp_d3d11_material_warp`
- `northstarcpp_d3d12_material_warp`
- `northstarcpp_d3d12_material_hardware`
- `northstarcpp_d3d12_warp`
- `northstarcpp_d3d12_hardware`
- `northstarcpp_d3d12_csm_hdr_warp`
- `northstarcpp_d3d12_csm_hdr_hardware`
- `northstarcpp_render_graph`
- `northstarcpp_deferred_math`
- `northstarcpp_render_tiled_lights`
- `northstarcpp_render_cascaded_shadows`

The isolated CSM math test verifies monotonic practical splits, the
finite/stable matrices, frustum-corner containment, and invalid-input
rejection. The direct renderer/material smoke tests additionally passed
with HDR+CSM+PCSS+bloom, and HDR with FXAA disabled.

The 1000-frame D3D12 WARP smoke stress test passed in the isolated build:

| Metric | CPU + Present / WARP |
| --- | ---: |
| p50 | 16.661 ms |
| p95 | 17.131 ms |
| p99 | 22.941 ms |

These are not GPU-only frame timings and include vsync. The stress test
uses its default feature flags, not the full HDR+CSM production scene.
Real GPU timestamps are available through the pre-existing optional
`NorthStarRenderTimingApi`. Hardware test sample timings do not
establish FPS performance in Night City.

## Limitations and next acceptance milestones

- No pixel-by-pixel visual parity evaluation against ZF or a production
  night-city scene with moving geometry. Passing a GPU readback smoke
  test proves execution, not correct appearance of every shadow.
- No alpha-tested/vegetation-specific shadow caster PSO or per-material
  shadow opacity handling yet. Material vertex alpha and CSM color
  masking remain separate work items.
- No local point/spot shadow maps, cube map shadow allocation,
  variable cascade counts or user-exposed CSM tuning UI.
- Camera-frustum extraction of some mesh asset batches remains outside
  this focused pass; shadow volume visibility must be verified across
  all ECS mesh paths, not just cube primitives.
- Exposure uses 64 stratified samples rather than a histogram or
  luminance percentile clamping; temporal response is per frame,
  not yet delta-time invariant. More robust eye adaptation remains.
- Bloom still uses one quarter-resolution bright-pass, not a
  multi-mip Gaussian or FFT bloom pipeline.
- No separate asynchronous compute queue, transient aliasing,
  GPU Render Graph executor, TAA/motion vectors, Hi-Z occlusion,
  denoised SSAO/GTAO, or depth of field.
- HDR output currently means **FP16 internal scene processing and
  SDR tonemapping**. The swapchain is RGBA8 SDR, not HDR10/PQ/HLG.
- No full Qt Editor/Player visual regression or production `dist`
  replacement has been carried out.
- No commit or push was performed. Existing unrelated Git changes
  were preserved.

## Source files

- `engine/northstarcpp/modules/render/shared/cascaded_shadows.hpp`
- `engine/northstarcpp/modules/render/shared/deferred_hlsl.hpp`
- `engine/northstarcpp/modules/render/shared/material_hlsl.hpp`
- `engine/northstarcpp/modules/render/shared/postfx_hdr_hlsl.hpp`
- `engine/northstarcpp/modules/render/shared/postfx_bloom_hlsl.hpp`
- `engine/northstarcpp/modules/render/d3d12/d3d12.cpp`
- `engine/northstarcpp/CMakeLists.txt`
- `tests/render_cascaded_shadows_smoke.cpp`
