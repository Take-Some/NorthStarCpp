# NorthStarCpp — GPU Render Graph Transient Aliasing & Cross-API Synchronization
Date: 2026-11-15

## Summary

This phase implements **real, opt-in, intraframe D3D12 GPU heap aliasing**
for one proven non-overlapping pair of render targets. It also extends
the shared Render Graph with **typed resource access declarations** and
compiled access transitions, with CPU-only Vulkan synchronization2 mapping.

D3D12 provider stays version **1.6.0**; the external `engine.render` ABI
stays **5**. No legacy renderer was introduced.

## 1. Typed, backend-neutral Render Graph resource uses

Source: `engine/northstarcpp/modules/render/shared/render_graph.hpp`

New `ResourceAccess` enum: `Undefined`, `ShaderRead`,
`ColorAttachment`, `DepthWrite`, `DepthRead`, `CopySource`,
`CopyDestination`, `Present`.

- Each `Pass` may declare an optional list of typed `ResourceUsage` entries
  in addition to the previous read/write/after dependency lists.
- `RenderGraph::compile()` now emits typed `AccessTransition` entries per
  scheduled pass while retaining the previous hazards, ordering and lifetime
  analysis.
- Imported resources can explicitly specify an initial access state.
- Duplicate typed resource uses, undefined uses and typed write operations
  not declared in the pass's write set are rejected.
- Existing four-field Pass initializers remain source-compatible.

The D3D12 graph (`renderer_pass_graph.hpp`) describes eight logical
resources, including separate G-buffer channels and a Bloom temporary.
The graph records the expected first/last use of each transient.

`renderer_pass_barriers.cpp` no longer uses the previous fixed
per-stage resource-state switch table: it lowers the graph-compiled
typed access transitions to D3D12 legacy states, resolving logical
resources through the current backend's GPU targets. Intra-pass
multi-step HDR/bloom operations still use the D3D12ResourceStates
tracker directly.

**Not yet implemented:** D3D12 Enhanced Barriers, state tracking for
individual image subresources, cross-queue ownership transfers, or
automatically compiling internal multi-step PostFX into standalone
subpasses. Vulkan GPU execution is separate future work.

## 2. Real intraframe D3D12 placed-resource aliasing

Source:
- `engine/northstarcpp/modules/render/d3d12/renderer_transient_alias.hpp`
- `engine/northstarcpp/modules/render/d3d12/renderer_transient_alias.cpp`
- `engine/northstarcpp/modules/render/d3d12/renderer_resources.cpp`

The initial, deliberately conservative pairing is:

| Logical resource | First stage | Last stage |
| --- | --- | --- |
| `GBufferNormal` | G-buffer | Deferred Lighting |
| `Bloom` | HDR/PostFX | HDR/PostFX |

There is no overlap. The D3D12 implementation consults the *actual
compiled Render Graph lifetime plan* and calls the pre-existing CPU
`plan_transient_resources()` with allocation size/alignment from
`ID3D12Device::GetResourceAllocationInfo`. It only proceeds when
the compatible resources map to the **same transient allocation slot**.

`CreateHeap` creates a single DEFAULT heap restricted to render targets.
`CreatePlacedResource` then creates both FP16 render target objects
at offset 0 inside the same heap. Their optimized clears, dimensions
and SRV/RTV descriptors remain distinct.

`D3D12_RESOURCE_BARRIER_TYPE_ALIASING` is recorded at resource ownership
switches (G-buffer -> Bloom and Bloom -> G-buffer across frames);
the existing state tracker separately manages legacy
`D3D12_RESOURCE_BARRIER_TYPE_TRANSITION` barriers.

On resize, the renderer waits for the graphics queue fence, releases
the two placed resources and their common heap, then creates a new
size-matched heap. **Placed resources never enter the committed texture
reuse pool independently**, preventing overlapping physical allocation
ownership from escaping into that cache.

The feature is opt-in pending wider game/validation coverage:

```cmd
set NORTHSTARCPP_ENABLE_TRANSIENT_ALIASING=1
set NORTHSTARCPP_ENABLE_BLOOM=1
set NORTHSTARCPP_ENABLE_HDR=1
set NORTHSTARCPP_TRACE_RENDER_POOL=1
```

It can run with HDR off as long as Bloom is active. With aliasing
disabled, existing committed render target allocation and the bounded
resize reuse pool remain in effect.

### Observed allocation reduction

For the tested pair, allocation info on the current environment showed:

| Resolution | Shared placed heap | Allocation saved |
| --- | ---: | ---: |
| 624 x 441, WARP | 2,228,224 bytes | 196,608 bytes |
| 384 x 261, WARP | 851,968 bytes | 65,536 bytes |
| 624 x 441, Hardware | 2,293,760 bytes | 196,608 bytes |
| 384 x 261, Hardware | 917,504 bytes | 65,536 bytes |

Those are **physical allocation size differences for this pair**, not
a reduction in the entire renderer footprint and not a frame-time
speedup claim.

## 3. Vulkan synchronization2 preparation

Source: `modules/render/shared/render_graph_vulkan_sync.hpp`

The CPU-only mapping from the **same ResourceAccess declarations** to
Vulkan Synchronization2 includes:

- `VkPipelineStageFlags2`
- `VkAccessFlags2`
- `VkImageLayout`
- `VkImageMemoryBarrier2`
- optional queue-family indices and image subresource range

It covers sampled shader input, color/depth writes, depth reads,
copy/transfer operations and swapchain presentation. A test verifies
color attachment -> shader read, depth usage, transfer layouts and
imported present -> color attachment without discarding prior layout.

The active NorthStarCpp render module tree currently contains
**D3D11 and D3D12 only**. There is no active Vulkan `engine.render`
provider here to receive or execute the mapping. This is a
tested CPU contract, **not** a GPU Vulkan smoke pass.

## 4. Tests and measured results

QA build: `out/renderer-state-qa-20261115`.

- Both D3D11 and D3D12 compiled and linked.
- **23 / 23 selected GPU and CPU CTest cases passed** in the final suite,
  including D3D12 aliasing on WARP and hardware, LDR Bloom without FXAA,
  HDR+CSM, D3D11, texture pooling, resource lifetime planner,
  Render Graph and Vulkan sync2 contract.
- `northstarcpp_d3d12_transient_alias_placed` creates real GPU placed
  resources on WARP, records **three aliasing barriers**, clears
  attachments, executes GPU commands, waits for a fence and verifies
  rejection of overlapping lifetimes.
- GPU readback smoke tests passed at multiple resolutions with aliasing
  explicitly enabled.
- A separately registered `northstarcpp_d3d12_transient_alias_stress_hardware`
  test passed **1000 frames** with HDR + CSM + Bloom + PCSS + aliasing.

### Stress-test timing caveat

The final registered hardware 1000-frame run reported **CPU+Present**:

| Metric | Measured |
| --- | ---: |
| p50 | 16.324 ms |
| p95 | 30.098 ms |
| p99 | 38.870 ms |

These timings include Present/synchronization and system variability.
No matched controlled A/B benchmark has demonstrated faster FPS from
aliasing. The 1000-frame test shows functional stability, not visual
parity or a general performance improvement.

The new Render Graph tests use **mandatory runtime checks** even in
Release builds with `NDEBUG`, so test success is not solely due to
disabled C `assert()` macros.

## 5. Remaining scope

This is the first validated intraframe physical alias pair, **not a
general-purpose allocator for arbitrary Render Graph resources**.

Next major steps:

1. Generalize placed allocation from two resources to N resources/slots,
   with per-slot active ownership and aliasing barriers.
2. Ensure D3D12/Vulkan memory requirements, resource flags, texture
   compatibility, heaps, formats, clear values, tiling and residency
   are validated by the backend rather than only a compatibility key.
3. Derive *all* resource-state changes from typed declarations,
   including subpasses within PostFX and graphics/compute queue
   synchronization.
4. Add debug-layer and Vulkan validation-layer integration, device-loss,
   long-run resize/streaming stress and GPU/CPU p50/p95/p99 traces.
5. Implement an actual Vulkan GPU provider using the shared graph,
   Vulkan image memory allocations and Sync2 pipeline barriers, without
   duplicating CPU dependency planning.

## 6. Deployment / repository policy

All changes remain in the current NorthStarCpp workspace. ABI 5 and
provider 1.6.0 remain unchanged. Production `dist` was not replaced.
No commit or push was performed. Existing unrelated uncommitted changes
were preserved; new source and QA files must be staged explicitly
when an authorized commit is eventually requested.
