# NorthStarCpp — Renderer parity audit and optimization iteration
**Date:** 2026-11-16  
**Reference:** user-supplied `renderer(6).zip` (289 entries, ~6.7 MB uncompressed).  
**Working tree:** `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`  
**QA build:** `_qa_editor_mixed` (native NVIDIA Quadro RTX 4000, D3D12 and Vulkan)

## 1. Reference inventory and scope

Source archive exposes a comprehensive mature renderer by **function**, not a
portable library: `Deferred/DeferredConfig.h`, `Deferred/GBuffer.*`,
`Deferred/DeferredLighting.*`, `Lights/TiledLighting.*`,
`RenderPhases/RenderPhaseCascadeShadows.*`, `SSAO.*`,
`PostProcessFX.*`, `AdaptiveDOF.*`, `MLAA.*`, `Mirrors.*`,
`RenderPhases/RenderPhaseReflection.*`, `RenderPhases/RenderPhaseWaterReflection.*`,
`LensArtefacts.*`, `newWater.cpp`, `Water.*`,
`RenderTargetMgr.*`, `RenderPhases/*`, `occlusion*.*`,
`PlantsGrassRenderer.*`, `DrawLists/*`.

The reference's GBuffer schema distinguishes four color targets plus
depth/stencil. Its fourth buffer includes separate ambient/emission/reflection
properties, which **cannot** be reconstructed from labels alone for modern PBR.
Do not port proprietary C++/shaders or legacy platform code. NorthStarCpp
GPLv3 implementation is independent and must retain current provider ABI.

### NorthStar baseline

**D3D12:** 3 physically packed MRTs (albedo+coverage, world normal+roughness,
emissive+metallic), depth position reconstruction, GGX lighting, tiled lights,
CSM/PCSS, optional 8-tap inline SSAO, FXAA, quarter-res bloom, temporal HDR
exposure and ACES-fitting tonemap, partial transient resource aliasing.
**Vulkan:** forward world/material/skin rendering and feature-specific GPU
code, with Render Graph and timestamps, but **not** the same complete
deferred GBuffer or post-processing chain as D3D12.

Adding a permanently allocated *unused* fourth MRT would increase memory
bandwidth, not improve quality. Additional material target(s) should be
capability-driven (e.g. clearcoat, transmission, anisotropy, AO, material
classification, motion vectors) with a documented layout, formats, shader
semantics and reference screenshots on both providers.

## 2. Implemented in this iteration

### Render Graph compiler

`modules/render/shared/render_graph.hpp`

- Replaced repeated all-prior-pass barrier scanning with
  per-resource last-access hazard tracking (RAW/WAR/WAW only).
- Consecutive read-only passes no longer emit redundant barriers.
- Replaced repeated O(P^2) topological selection with stable-min-ID
  dependency queue; ordering is deterministic and scales
  O((P + E) log P), where E is dependency count.
- Resource lifetime intervals and typed usage transitions are preserved;
  the Vulkan synchronization2 executor and D3D12 lifetime/alias planner
  continue to receive the same typed access contract.
- Added a graph stress regression with **2048 readers** and final overwrite
  verifying barrier minimality, dependency ordering and reproducibility.

### D3D12 tiled-light culling cache

`modules/render/shared/tiled_light_cache.hpp` (new);
`modules/render/d3d12/{renderer_private.hpp,renderer_pass_deferred.cpp,renderer_resources.cpp}`.

- Cached conservative tile classification and persistent GPU light index
  uploads across identical view-projection matrices, camera eye, viewport
  size and full light values.
- Any camera/light/viewport change forces a fresh classification; GPU
  upload reallocation explicitly invalidates the cache.
- No provider ABI or scene format change. No possibility of silently
  dropping lights when a tile overflows (existing fallback retained).
- Added tests for hits/misses, camera change, light change, resolution
  change, projection change, GPU invalidation and invalid inputs.
- This eliminates unnecessary CPU work for static scenes. **Do not infer
  Night City FPS gains without a matched before/after profile**, especially
  while the game camera continuously moves.

### Bloom / HDR eye adaptation

`modules/render/shared/{postfx_bloom_hlsl.hpp,postfx_hdr_hlsl.hpp}`;
`modules/render/d3d12/{renderer_private.hpp,renderer_pass_postfx.cpp,renderer_resources.cpp}`.

- Bright-pass soft knee, normalized **13-sample** quarter-resolution
  highlight filter and bounded isolated firefly energy. No new full-screen
  pass and no additional persistent color RT.
- HDR adaptation uses actual frame elapsed seconds (clamped) with separate
  time constants for eye opening / closing; no frame-count-dependent
  constant lerp.
- Resizing resets temporal timing history.
- Legacy bloom, FXAA, HDR enable controls and the UI-over-tonemap ordering
  remain unchanged.

### D3D12 GBuffer debug visualization

`modules/render/shared/deferred_hlsl.hpp`;
`modules/render/d3d12/{renderer_private.hpp,renderer_device.cpp,renderer_pass_deferred.cpp}`.

- Optional `NORTHSTARCPP_GBUFFER_DEBUG=albedo|normal|roughness|metallic|emissive|depth|ao`
  (or `off`). The normal path leaves shading unchanged.
- The deferred pixel shader displays channels directly without new MRT,
  CPU readback, or switchover to a legacy rendering path.
- Perceptually compressed depth view is for authoring visibility; normal
  deferred world-position reconstruction and depth values remain unchanged.
- Added CTest hardware smoke coverage for `albedo`, `normal`,
  `roughness`, `depth`, `ao`. `metallic` and `emissive` visual modes
  exist but need material-specific pixel reference tests: a default
  nonmetallic/nonemissive primitive correctly produces black in those modes.

### Automated effect QA

`engine/northstarcpp/CMakeLists.txt`, `tests/render_graph_smoke.cpp`,
`tests/render_tiled_lights_smoke.cpp`.

Registered 5 GBuffer debug tests plus:
- `northstarcpp_device_probe_d3d12_postfx_hardware`: native --full with
  BLOOM/HDR/SSAO/SHADOWS/PCSS enabled
- `northstarcpp_device_probe_d3d12_postfx_stress`: 100 native GPU frames
  under the same effect configuration

## 3. Measured acceptance

- Render Graph / Vulkan sync2 / Vulkan executor / transient resource planner
  / tiled lighting regression: **5/5 PASS**.
- GPU D3D12 hardware and Vulkan hardware plus each 1000-frame stress:
  **4/4 PASS** (part of final 9/9 suite, total 40.56 s).
- D3D12 GBuffer Debug native GPU hardware: **5/5 PASS**, 8.23 s.
- D3D12 combined PostFX native GPU: **2/2 PASS**, 5.09 s.
- Manual `--full` native D3D12 with HDR, Bloom, SSAO, CSM, PCSS:
  **PASS**, adapter NVIDIA Quadro RTX 4000, not software.
- Manual native 1000-frame all-effects D3D12 GPU timestamps:
  GPU p50/p95/p99 **0.813696 / 0.83773 / 0.854461 ms**;
  CPU draw p50/p95/p99 **1.91395 / 2.39253 / 2.66261 ms**.
  This is the renderer's synthetic device probe, **not** Night City
  10,000-object or production end-to-end frame time.

## 4. Features not yet at reference parity

| Area | Priority | Required work / acceptance |
| --- | --- | --- |
| Vulkan deferred | P0 | Native multi-target GBuffer, depth SRV, PBR tiled resolve, shadows, parity screenshots / real GPU tests |
| GBuffer extensions | P0 | Typed material attributes (AO, IBL, clearcoat, transmission, anisotropy), compact layouts, velocity history, valid masks, alpha test |
| SSAO | P0 | Separate half-resolution AO + depth/normal bilateral blur, temporal history rejection, p95 GPU cost |
| Temporal rendering | P0 | Depth/motion-vector output, TAA/TAAU and history reset on teleport/resize, dynamic-resolution integration |
| Bloom and HDR | P0 | Multi-mip bloom pyramid + upsample, GPU luminance histogram, temporal exposure rejection and color grading |
| Shadows | P1 | Per-light point/spot shadows, GPU caching, stabilization, PCF/PCSS quality modes; eventual VSM |
| Reflections | P1 | Depth pyramid / SSR, fallbacks to environment probes, planar water/mirror reflections |
| Volumetrics | P1 | Fog, light shafts, atmospheric integration with 3D light culling |
| DOF / Motion blur | P1 | GPU CoC + tile gather, object/velocity motion blur with camera cuts and UI isolation |
| Advanced PostFX | P1 | Vignette, chromatic aberration, film grain, lens distortion, damage/screen-space effects via runtime configurable providers |
| Draw submission | P1 | GPU frustum/HZB occlusion, meshlet/indirect draws, persistent descriptors, PSO precache |
| Profiling | P0/P2 | Vulkan+D3D12 comparable pass-by-pass GPU/CPU timeline, ≥1000 frames, 10k+ drawables, p50/p95/p99 cold/warm |
| Reliability | P0 | resize, device-lost, minimize/Alt+Tab, hot reload, long-run allocation/leak checks |

Do not call this engine UE5/archived-renderer-equivalent yet. Do not
consider the Vulkan backend interchangeable with D3D12 PostFX or GBuffer
until functional GPU pixel comparisons under the same scene pass.

## 5. Integration and release

All changes are in the current connected NorthStarCpp working tree.
QA builds remain under `_qa_editor_mixed`. No production EXE/DLL
replacement; no git commit/push; do not roll back other concurrent work.
Next implementation phase should prioritize Vulkan deferred PBR and
screen-space effects infrastructure instead of adding placeholder toggles.
