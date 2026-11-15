# NorthStarCpp — Render Graph N-Resource Aliasing, Declarative PostFX, Vulkan Sync2 and GPU Profiling

**Date:** 2026-11-15
**Repository:** current NorthStarCpp working tree
**D3D12 provider:** 1.6.0 (unchanged)
**Render ABI:** 5 (unchanged)
**QA directory:** `out/renderer-state-qa-20261115`

## Executive summary

The previous two-resource D3D12 transient alias path was generalized into an
**N-resource placed-texture arena**. The backend-neutral Render Graph now
schedules **nine stages**, including independent forward translucency, bloom,
exposure, tonemapping, and SDR/FXAA composition. A CPU-testable Vulkan Sync2
command-buffer executor and a capability-guarded D3D12 Enhanced Barriers
mapping/probe were added. An opt-in, **1,000-frame per-pass GPU timestamp**
collection mode was implemented in the hardware smoke harness.

**Not accomplished:** an actual Vulkan `engine.render` provider, production
D3D12 Enhanced Barrier integration on a capable driver, or Night City scene
profiling. No FPS increase is claimed.

## 1. N-resource D3D12 transient placement

**Source:**
- `modules/render/d3d12/renderer_transient_alias.hpp`
- `modules/render/d3d12/renderer_transient_alias.cpp`
- `modules/render/shared/transient_resource_planner.hpp`

The new API takes a vector of `D3D12TransientTextureRequest` descriptions
rather than assuming exactly two resources.

Pipeline:
1. For each graph resource, validate that it is non-imported, 2D, single
   mip/slice, non-MSAA, render-target capable, and a supported format.
2. Query `GetResourceAllocationInfo` for the exact GPU size/alignment.
3. Partition requests by format, flags, sample count and resource dimension.
4. Use the shared lifetime planner to assign a physical slot. Two logical
   resources can reuse a slot only when their scheduled lifetimes are
   **strictly non-overlapping** and the slot is compatible and large enough.
5. Align and pack the slots at distinct offsets **inside one D3D12 heap**.
   Within a slot, multiple sequential logical resources use the same offset.
6. Construct individual `CreatePlacedResource` objects at the planned
   offsets and emit aliasing barriers when a slot changes active owner.
7. Require the caller to wait on its graphics-queue fence and release placed
   resources before releasing the heap on resize/shutdown.

A source-compatible two-resource overload remains in place. It rejects pairs
that cannot share one physical slot, preserving the existing G-buffer Normal /
Bloom path. The existing committed resource reuse pool remains separate from
the placed-resource arena.

**WARP acceptance:**
- The previous 2-resource test still passes, including GPU clears, barriers,
  execution, fence and overlapping-lifetime rejection.
- The generalized test successfully placed **5 logical resources in 1 heap,
  with 2 distinct physical slots**.
- Four sequential resources used the same physical slot, while the fifth,
  overlapping resource occupied the second physical slot.
- Five aliasing barriers were recorded and executed on WARP; GPU fence
  completion was verified.
- Existing opt-in HDR+CSM+Bloom aliasing paths passed on hardware and WARP.

The N-resource allocator is implemented and exercised by tests; the actual
production scene graph currently uses only the original non-overlapping
G-buffer Normal / Bloom pair. Other targets need explicit opt-in wiring and
format/lifetime QA before being aliased.

## 2. Declarative PostFX subpasses

**Source:**
- `modules/render/d3d12/renderer_pass_graph.hpp`
- `modules/render/d3d12/renderer_pass_postfx.cpp`
- `modules/render/d3d12/renderer_frame.cpp`
- `modules/render/d3d12/renderer_pass_barriers.cpp`
- `modules/render/d3d12/renderer_private.hpp`

The monolithic PostFX function was split into independent operations recorded
by the same compiled Render Graph as the scene passes:

| Stage | GPU work |
| --- | --- |
| Shadow | CSM directional depth array |
| G-buffer | Opaque scene/material MRT |
| Deferred | G-buffer lighting resolve |
| Translucency | Forward translucent surfaces; opaque end timestamp |
| Bloom | HDR/LDR scene SRV setup, quarter-resolution bloom extraction |
| Exposure | HDR 1x1 exposure metering and history |
| Tonemap | HDR -> LDR tonemapping |
| Composite | FXAA or direct scene copy and SDR composition |
| Present | Native HUD, timing readback, Present |

The graph contains **10 logical resources** including separate G-buffer
channels, bloom, exposure and the tone-mapped LDR target. Typed access
declarations drive pass-boundary barriers. Frame/feature-gated resource
resolution ensures no invalid state changes when HDR, bloom or FXAA is
disabled.

Within a PostFX operation, multiple sequential resource-state transitions
are still managed through the centralized D3D12 state tracker. Each stage is
scheduled independently, but complete GPU barrier automation down to
individual draw calls and transient image subresources is not yet implemented.

**Compatibility QA:** D3D12 hardware, WARP, HDR+CSM, LDR bloom without FXAA,
HDR without FXAA, fully disabled PostFX and opt-in aliasing all passed.
Render Graph tests now check nine pass IDs and ten logical resources, including
exposure and tone mapping transitions, in Release mode.

## 3. D3D12 Enhanced Barriers preparation

**Source:** `modules/render/d3d12/renderer_enhanced_barriers.hpp`

The mapping converts a backend-neutral `ResourceAccess` to
`D3D12_BARRIER_SYNC`, `D3D12_BARRIER_ACCESS` and
`D3D12_BARRIER_LAYOUT`, then prepares a full-subresource
`D3D12_TEXTURE_BARRIER`. The device capability probe checks
`D3D12_FEATURE_D3D12_OPTIONS12.EnhancedBarriersSupported`.

A separate GPU test creates a COMMON-layout RT texture, requests
`ID3D12GraphicsCommandList7`, records transition/clear/transition using
`Barrier`, and synchronizes a fence **only on a supported device/runtime**.

**Current environment:** both WARP and the tested hardware device returned
EnhancedBarriersSupported == FALSE. Therefore both GPU tests were **SKIPPED**
(CTest return code 77), not counted as passes. The production D3D12 frame
continues to use tested legacy `ResourceBarrier` transitions and aliasing
barriers. Mixing legacy and enhanced barriers on the same subresource without
proper COMMON-layout interop is intentionally avoided.

This stage establishes the API/mapping and capability handling; **runtime
Enhanced Barrier execution is not validated or enabled yet**.

## 4. Vulkan Sync2 command-buffer executor foundation

**Source:**
- `modules/render/shared/render_graph_vulkan_sync.hpp`
- `modules/render/shared/render_graph_vulkan_executor.hpp`
- `tests/render_graph_vulkan_executor_smoke.cpp`

The portable executor accepts a compiled Render Graph plan, image binding
table, recording callback, valid `VkCommandBuffer` and an externally
resolved `PFN_vkCmdPipelineBarrier2`.

For each scheduled pass it:
- Converts typed access transitions into `VkImageMemoryBarrier2` values
  with appropriate stage/access/layout masks and subresource range.
- Emits an image-barrier dependency through the supplied Vulkan function.
- Calls the pass recorder in the compiled order.
- Rejects missing command buffer, barrier function, image bindings and
  invalid subresource ranges.

The CPU test uses a mock Vulkan barrier function and verifies pass order,
barrier count, present->color and color->present layout transitions, and
invalid binding rejection.

**This is not an installed Vulkan GPU provider.** It does not create VkDevice,
swapchain, graphics PSOs, image allocations, queue submissions or GPU fences.
It is a reusable command-buffer recording layer only. There was no actual
Vulkan GPU render test in this stage.

## 5. Per-pass GPU timestamp profiling (1,000 frames)

**Source:** `tests/render_d3d12_smoke.cpp`.

New explicit QA flags:

```cmd
northstarcpp_d3d12_smoke.exe <QA DLL path> --hardware --stress --gpu-profile
```

The smoke harness reads and validates the existing `NorthStarRenderTimingApi`
on each frame, verifies segment totals and computes p50/p95/p99 for:
opaque, deferred, postfx, HUD, total GPU time. The 1,000-frame test is
registered as `northstarcpp_d3d12_gpu_timestamp_profile_hardware`.

**Hardware test result, current simple QA scene with HDR+CSM+PCSS+Bloom+aliasing:**

| GPU segment | p50 (ms) | p95 (ms) | p99 (ms) |
| --- | ---: | ---: | ---: |
| Total GPU | 0.5601 | 0.6858 | 0.8510 |
| Opaque | 0.3334 | 0.3864 | 0.4765 |
| Deferred | 0.0476 | 0.1033 | 0.2228 |
| PostFX | 0.1740 | 0.2146 | 0.2416 |
| HUD | 0.0013 | 0.0016 | 0.0016 |

The sampling mode calls a **synchronized timing read** after each draw. The
CPU draw-call time in that mode excludes the profiling readback stall and
should not be compared to ordinary CPU+Present frame times. Individual GPU
pass percentiles cannot be summed to produce the total GPU percentile.

This result characterizes only the small smoke scene. It **does not verify
Night City frame time**, real-world stress, animated NPCs or long-range
transport physics. The test is suitable as a reusable basis for proper
game-scene GPU profiling once the Editor/Player run is instrumented.

## 6. Acceptance matrix

In the final combined regression run **24/24 selected tests passed**:
- D3D11 and D3D12 material tests on WARP and hardware
- D3D12 native WARP and hardware
- Shadow/HDR/PCSS paths
- Real placed aliasing, 5-resource WARP placed allocator and target pooling
- Bloom without FXAA and HDR without FXAA
- Disabled PostFX paths
- Render Graph (including typed D3D12 PostFX subpasses)
- CPU transient memory planner and Vulkan Sync2/executor CPU tests
- Deferred math, tiled-light and cascaded-shadow math

Separately, `northstarcpp_d3d12_gpu_timestamp_profile_hardware` passed
1,000 synchronized valid GPU timestamp frames.

The two D3D12 Enhanced Barriers GPU tests were **SKIPPED**, as neither
tested adapter advertised support. These skips are separate from the
24 passing tests.

**Full Windows Editor/Player integration, all-source clean builds, Vulkan
GPU execution and Night City-specific GPU profiling remain unverified.**

## 7. Remaining work

1. Connect more compatible graph transients to the generalized placed-resource
   arena and verify actual GPU memory reduction under real workloads.
2. Finish typed access declarations for all intra-pass resource operations,
   including exposure read/write history and copy operations.
3. Validate and enable Enhanced Barriers only on a runtime/device reporting
   support, with correct legacy COMMON-layout interop or an entirely
   enhanced resource-lifecycle path.
4. Implement an actual replaceable Vulkan render provider consuming the
   shared graph and Vulkan command-buffer executor.
5. Profile the running Night City project across at least 1,000 valid frames
   under representative animation, streaming, transport and collision loads.
6. Compare cold/warm runs, resize stresses, memory residency and
   p50/p95/p99 under matched hardware conditions before claiming speedups.

## 8. Deployment and change control

Changes are local to the current NorthStarCpp source tree. The external
render ABI is still 5, the D3D12 provider version is still 1.6.0.
Production `dist` was **not** replaced. Neither a Git commit nor a push
was performed. Existing unrelated unstaged changes were preserved.
