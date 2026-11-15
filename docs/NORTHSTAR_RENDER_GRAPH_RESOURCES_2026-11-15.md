# NorthStarCpp — GPU Render Graph Resource Management

**Date:** 2026-11-15
**Provider:** D3D12 (existing plugin, ABI 5 unchanged)
**Scope:** state transitions, render graph stage barriers, transient allocation analysis, fence-safe render target reuse.

## 1. Architecture and implementation

### D3D12ResourceStates

**Files:**
- `engine/northstarcpp/modules/render/d3d12/renderer_resource_states.hpp`
- `engine/northstarcpp/modules/render/d3d12/renderer_resource_states.cpp`

A per-frame resource-state tracker receives the known initial state of each persistent target (backbuffer, depth, shadow array, G-buffer, scene color, HDR scene, bloom and two exposure targets). It computes an actual `D3D12_RESOURCE_BARRIER` only when a requested state differs from its current tracked state.

The tracker rejects transitions involving unknown resources, rejects conflicting initial states, and counts emitted barriers. The frame validates expected final states for all active targets before returning. All direct `ResourceBarrier()` calls previously embedded in the six frame/pass recording files were replaced by tracked transitions.

**Important:** This is currently D3D12 *legacy* resource barriers. It does not yet implement the D3D12 Enhanced Barriers API, subresource-level state tracking or cross-queue GPU synchronization.

### Render Graph stage barrier executor

**File:** `engine/northstarcpp/modules/render/d3d12/renderer_pass_barriers.cpp`.

Render Graph dispatch now calls `apply_graph_resource_states(stage,context)` before each compiled stage:

| GPU Stage | Required pass-boundary states |
|---|---|
| Shadow | Shadow array -> DEPTH_WRITE |
| G-buffer | Shadow array -> PIXEL_SHADER_RESOURCE; G-buffer attachments -> RENDER_TARGET |
| Deferred | G-buffer attachments and depth -> PIXEL_SHADER_RESOURCE |
| PostFX | Scene depth -> DEPTH_WRITE for forward translucency |
| HUD/Present | Frame finalization; backbuffer transitions to COPY_SOURCE for readback and then PRESENT |

HDR, Bloom, exposure and LDR scene-copy transitions inside the PostFX stage are also routed through the same tracker.

**Limit:** The stage-required states are specified in the D3D12 executor and are not yet automatically inferred solely from the backend-neutral `RenderGraphPlan::barriers` vector. Render Graph compiles ordering/hazards, while the D3D12 stage executor applies backend-specific state declarations. This is **not** a complete automatic cross-API barrier lowering pipeline.

### Backend-neutral transient allocation planning

**Files:**
- `engine/northstarcpp/modules/render/shared/transient_resource_planner.hpp`
- `tests/render_transient_resource_planner_smoke.cpp`

The planner uses `RenderGraphPlan::lifetimes` (first/last scheduled use). It accepts per-resource sizes, power-of-two alignment and an explicit compatibility key.

- The physical slot may be reused only if the previous resource's last use is **strictly less** than the next resource's first use.
- Imported resources cannot be aliased.
- Overlapping lifetimes cannot share a slot.
- Incompatible format/usage classes or alignment cannot share a slot.
- Invalid, unused, duplicate and overflowing resource requests are rejected.
- The result provides resource-to-slot mappings, reserved bytes and peak slot residency.

The planner is reusable by other backends, including Vulkan. However, it currently produces a **CPU-side proposal**, not D3D12 placed-resource allocations and aliasing barriers. There is no unsafe in-frame physical aliasing enabled by this change.

### Real GPU target reuse on resize

**Files:**
- `engine/northstarcpp/modules/render/d3d12/renderer_texture_pool.hpp`
- `engine/northstarcpp/modules/render/d3d12/renderer_texture_pool.cpp`
- `engine/northstarcpp/modules/render/d3d12/renderer_resources.cpp`

A separate **128 MiB capped pool of committed D3D12 render targets** now retains completed targets after resize and reuses them when the resolution/format reoccurs.

Safety contract:
1. `RendererState::resize` waits for the GPU queue fence **before** collecting old render targets.
2. The pool takes exclusive COM ownership using `Detach`/`Attach`. A borrowed target is no longer eligible for another borrower.
3. A cached resource matches only when its exact D3D12 resource description (including normalized default alignment), intended state and **actual originally specified optimized clear value** match.
4. The pool tracks the optimized clear metadata for each live allocation; it is not inferred from format alone.
5. If no match exists, a fresh committed resource is allocated. The oldest cache entries are evicted to remain within the hard budget.
6. New RTV/DSV/SRV descriptors are constructed against the returned resource. GPU resources are not aliased simultaneously.

The pool handles scene color, HDR scene color, bloom, exposure, depth and G-buffer textures. It does **not** pool swapchain buffers or texture resources from other providers.

**Optional resource-pool diagnostics:**

```cmd
set NORTHSTARCPP_TRACE_RENDER_POOL=1
```

At resize, the renderer reports resolution, cumulative cache hits, cached bytes, cached target count and the 128 MiB limit.

## 2. Regression and defect resolution

Initial release-mode QA exposed two issues, which were fixed before acceptance:

- The texture pool's first implementation moved a `ComPtr` without proving that the source lost ownership. The pool now uses explicit COM `Detach()/Attach()`.
- Initial descriptor matching did not normalize D3D12's default 64 KiB resource placement alignment when a creation description used Alignment=0 and the device returned the explicit value. Matching now normalizes both sides.

The two new acceptance tests have **mandatory runtime assertions**, independent of `NDEBUG` in Release. Three additional Hardware tests check HDR with FXAA off, LDR bloom with FXAA off and entirely disabled PostFX; these guard against incorrect scene-target and swapchain state transitions.

The dedicated WARP test **northstarcpp_d3d12_texture_pool** demonstrates actual GPU resource pointer reuse and reports **2 cache hits** after exercising live resource exclusion, matching/incompatible formats, optimized clear value matching and the state tracker. The CPU transient-planner test verifies strict lifetime separation and invalid cases.

### Final selected CTest run

**18/18 passed** in the isolated Release QA directory:

`out/renderer-state-qa-20261115`

- `northstarcpp_d3d11_material_warp`
- `northstarcpp_d3d11_material_hardware`
- `northstarcpp_d3d12_material_warp`
- `northstarcpp_d3d12_material_hardware`
- `northstarcpp_d3d12_texture_pool`
- `northstarcpp_d3d12_warp`
- `northstarcpp_d3d12_hardware`
- `northstarcpp_d3d12_csm_hdr_warp`
- `northstarcpp_d3d12_csm_hdr_hardware`
- `northstarcpp_d3d12_hdr_no_fxaa_hardware`
- `northstarcpp_d3d12_bloom_no_fxaa_hardware`
- `northstarcpp_d3d12_no_postfx_hardware`
- `northstarcpp_render_graph`
- `northstarcpp_render_transient_resource_planner`
- `northstarcpp_d3d12_pass_graph`
- `northstarcpp_deferred_math`
- `northstarcpp_render_tiled_lights`
- `northstarcpp_render_cascaded_shadows`

D3D11 and D3D12 DLLs were compiled in the QA directory, and the corresponding targeted tests passed.

### 1000-frame WARP stress

The updated D3D12 WARP stress test passed, reporting CPU + Present milliseconds:

| Percentile | Duration |
| --- | ---: |
| p50 | 16.670 ms |
| p95 | 26.952 ms |
| p99 | 72.595 ms |

These are test-scene measurements including synchronization, not GPU-only timings or a controlled before/after speedup. The p95/p99 regression relative to earlier measurements must be independently profiled and is **not** an accepted performance improvement.

## 3. Outstanding work

1. **True in-frame transient aliasing:** implement placed-resource heaps, aliasing barriers, subresource compatibility and GPU validation before using the CPU allocation plan to share real GPU memory.
2. **Fully declarative barriers:** derive desired D3D12/Vulkan states from typed Render Graph access declarations, replacing stage-specific state declarations and ad hoc intra-pass transitions.
3. **D3D12 Enhanced Barriers / Vulkan sync2:** use modern APIs where supported and safely fall back where required; support graphics/compute queue ownership transfer.
4. **Texture pool validation:** repeated resize across many resolutions, long-run cache eviction, memory growth and device-loss recovery.
5. **Hardware profiling:** per-pass GPU timestamps, >=1000 valid frames of p50/p95/p99, real Night City geometry/material load and cold/warm comparison.
6. **Vulkan integration:** reuse the CPU graph and transient lifetime planner, but implement VkImage layouts, pipeline stage/access masks, image barriers and safe allocator policy in its provider.

## 4. Deployment policy

All changes are confined to the active NorthStarCpp working tree. No Git commit or push; no production `dist` replacement. Previously existing uncommitted edits were preserved. ABI 5 remains unchanged.
