# NorthStarCpp — Vulkan Sun Lighting, Contact Shadows and Film-Grain Optimization
**Date:** 2026-11-17
**Scope:** rendering providers only; independent NorthStar implementation.
**Reference:** user attachment `renderer(6).zip` (289 entries, 6,737,324 uncompressed bytes). Reference inventory includes deferred/tiled lighting, cascaded shadows, SSAO and PostProcessFX; no external source code or shaders were copied.

## Changes

### Vulkan: directional sun no longer disappears when local lights exist
`modules/render/vulkan/deferred_resolve.frag` now computes the baseline GGX sun term independently of the dynamic-light count. Dynamic point/spot lights are accumulated afterwards when available. Previously the sun was present only in the `count == 0` branch, so adding a light disabled sunlight.

### Vulkan: opt-in short-range screen-space contact shadows
- Enable `NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1` and `NORTHSTARCPP_ENABLE_VULKAN_CONTACT_SHADOWS=1` **before creating the Vulkan provider**.
- Requires the sampled GBuffer depth already used by Vulkan deferred shading; no new texture, render target, descriptor or ABI.
- Ray marches from visible surfaces toward the sun at up to 8 positions, with depth reconstruction, a short world-distance cap, thickness rejection, normal offset and screen bounds validation.
- The contact term attenuates **only directional sun**; it does not darken local lights, ambient or emissive.
- Debug mask: `NORTHSTARCPP_GBUFFER_DEBUG=contact_shadow` (white = unoccluded, darker = contact shadow).
- Disabled by default: visible-screen-only geometry cannot cast shadows from outside the viewport, and this is **not** world-space cascaded shadow mapping, VSM or per-light shadow maps. Artifact/quality QA on representative Night City scenes is still required.

### D3D12: avoid redundant shadow-map samples
`modules/render/shared/deferred_hlsl.hpp` skips the expensive 3-cascade PCF/PCSS sampling for pixels where direct solar NoL is zero. The existing CSM transition and shadow quality behavior for sunlit surfaces are unchanged.

### Both providers: deterministic Film Grain hash
`modules/render/shared/postfx_hlsl.hpp` and `modules/render/vulkan/deferred_resolve.frag` replace transcendental sine-based per-pixel grain noise with a matched 32-bit integer avalanche hash. No additional texture or pass is required. Grain stays opt-in through the existing Filmic settings; the identity look remains neutral. This is a shader instruction optimization, not a measured end-to-end frame-time improvement.

`tools/generate_vulkan_deferred_spirv.py` was run to regenerate `modules/render/vulkan/deferred_spirv.hpp` for both GBuffer variants.

## QA results
Build directory: `_qa_editor_split`, not the production `dist`.

- glslc `--target-env=vulkan1.3` regenerated SPIR-V successfully.
- CMake/Ninja compiled and linked `northstarcpp_render_vulkan`, `northstarcpp_render_d3d12` and `northstarcpp_render_vulkan_smoke`.
- With `NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1`, `NORTHSTARCPP_ENABLE_VULKAN_SSAO=1`, `NORTHSTARCPP_ENABLE_VULKAN_CONTACT_SHADOWS=1`: `northstarcpp_vulkan_hardware`, `northstarcpp_vulkan_stress_hardware` and `northstarcpp_vulkan_gpu_profile_hardware` — **3/3 PASS**.
- Additional GPU probes: `northstarcpp_device_probe_d3d12_postfx_hardware`, `northstarcpp_device_probe_d3d12_postfx_stress` and `northstarcpp_device_probe_vulkan_hardware` — **3/3 PASS**.
- 1000-frame Vulkan GPU profile, synthetic QA scene, with deferred and SSAO on:
  - Contact OFF: p50 0.1211 ms, p95 0.1281 ms, p99 0.1293 ms.
  - Contact ON: p50 0.0980 ms, p95 0.1283 ms, p99 0.1296 ms.
  - p50 difference is likely run-to-run effects; **do not infer acceleration** from this run. p95/p99 essentially unchanged in this particular scene. This test does not validate full open-world frame time or detailed visual accuracy.

## Remaining work
1. Real Vulkan directional cascaded shadow maps, per-light point/spot shadows and parity with D3D12; contact shadows cannot replace these.
2. Optional depth/normal-aware AO denoise, half-resolution AO and temporal accumulation.
3. Full Vulkan HDR bloom and scene-color Filmic composite after translucency, with chromatic aberration applied consistently.
4. More advanced post effects (depth of field, velocity-based motion blur, volumetric fog, reflection probes, SSR), feature-negotiated without forcing extra resources on all presets.
5. Night City controlled A/B capture: identical view, lighting, effect toggles, resolution and driver; 1000+ frames p50/p95/p99, GPU pass timings, VRAM, validation layers, and user-visible visual diff.

No production EXE/DLL replacement, no commit, no push. Existing unrelated uncommitted editor, physics and game work must remain untouched.
