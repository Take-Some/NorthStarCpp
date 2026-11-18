# NorthStarCpp — Vulkan CSM + persistent Pipeline Cache
Date: 2026-11-17. QA directory: `_qa_renderer_20261117`.

## Implemented
- Optional `NORTHSTARCPP_ENABLE_VULKAN_CSM=1` (requires `NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1`): three 1024x1024 D32 cascade layers, depth-only shadow caster pipelines for primitive, opaque material and skinned vertex layouts, log/linear cascade splits, light texel snapping, 3x3 PCF, cascade fading and sun shadow visibility in deferred GGX lighting.
- Compatible with GBuffer v1/v2, half-resolution SSAO and existing contact shadows; new debug view `NORTHSTARCPP_GBUFFER_DEBUG=sun_shadow`.
- Added resource lifetime, depth image descriptor and fragment sampled-depth barrier to Vulkan Render Graph.
- Persistent `VkPipelineCache` used by graphics PSOs, including material, UI, CSM and deferred pipelines; mutex guards concurrent pipeline compilation. Cache saved under `%LOCALAPPDATA%\KaylasSystems\NorthStarCpp\PipelineCache`; validates Vulkan cache header UUID/vendor/device and caps input at 64 MiB. Disable with `NORTHSTARCPP_DISABLE_VULKAN_PSO_CACHE=1`.

## Verification
- Native Vulkan and D3D12 renderer builds: PASS.
- CSM x GBuffer v1/v2 x SSAO enabled/disabled: 4/4 hardware PASS.
- SSAO x GBuffer v1/v2: 2/2 PASS.
- Render Graph / Sync2 / Vulkan graph executor: 3/3 PASS.
- D3D12 PostFX hardware probe: PASS. Total targeted regression 10/10 PASS.
- 1000-frame CSM stress, Vulkan GPU profile and Vulkan validation tests: 3/3 PASS.
- Vulkan cache created on first run (175,751 bytes) and reused on second run: PASS.
- Synthetic Vulkan GPU time p50/p95/p99, CSM OFF: 0.1581/0.1663/0.1708 ms; CSM ON: 0.2178/0.2252/0.2268 ms. Cost at p95 +0.0589 ms. This is not a Night City benchmark or evidence of improved FPS.

## Remaining / not yet at production parity
Alpha-masked material casters do not yet have correct cutout shadow passes; non-skinned masked batches are skipped rather than drawn as opaque shadows. World-surface forward fallback is not covered by Vulkan Deferred CSM. Per-cascade caster culling, shadow caching and city-scale QA remain.

HDR/FP16 scene color, Bloom and Filmic compositor after transparency, point/spot light shadow maps, temporal SSAO, TAA, motion vectors, DoF, volumetric fog, SSR and HZB/GPU culling are not implemented in this iteration. Pipeline Cache works, but startup latency improvements are not yet measured.

No production dist replacement, commit or push. All other working-tree edits left untouched.
