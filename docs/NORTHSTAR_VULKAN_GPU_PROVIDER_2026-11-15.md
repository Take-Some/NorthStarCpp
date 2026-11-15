# NorthStarCpp — Vulkan GPU Provider First Runtime Milestone

**Date:** 2026-11-15
**Working tree:** `C:\Users\AidenFox\Documents\NorthStarCpp`
**Renderer plugin:** `engine.render`, provider `vulkan` version `0.1.0`
**External Render ABI:** `5` (unchanged)
**QA build:** `out/renderer-state-qa-20261115/render_vulkan.dll`

## 1. What is implemented and working

Created the first native Windows Vulkan GPU renderer in:

`engine/northstarcpp/modules/render/vulkan/`

Files:
- `vulkan.cpp` — stable C API provider module, ABI version negotiation,
  optional triangle and GPU timestamp extensions.
- `renderer_private.hpp` — internal Vulkan state; no Vk objects escape ABI.
- `renderer_device.cpp` — instance, Win32 surface, adapter capability probe,
  logical device, graphics/present queue, swapchain and resize, graphics
  pipeline, device/host-visible memory, shader modules, debug messenger,
  GPU timing query pool and exception-safe resource cleanup.
- `renderer_frame.cpp` — scene vertex buffer upload, basic colored triangle
  and cube geometry, dynamic rendering, Vulkan Sync2 image transitions via the
  shared Render Graph executor, GPU readback, presentation and timestamps.
- `basic.vert` / `basic.frag` — original GLSL shaders; both compiled to
  valid SPIR-V with the installed Vulkan SDK's `glslc`.
- `shader_spirv.hpp` — embedded SPIR-V words for reproducible plugin loading.

The provider retains `NorthStarRenderApi` ABI 5 and exports the same
`northstar_module_query` negotiation entrypoint used by existing D3D
render providers. Triangle and GPU timestamp APIs are optional module
extensions; unsupported extension versions are rejected.

The Vulkan plugin is **not selected as the engine default**. D3D11 and
D3D12 keep their existing registration and ABI.

### Native GPU execution

Confirmed on:
- NVIDIA GeForce GTX 750
- NVIDIA Vulkan driver 582.66
- Vulkan device API 1.4.312
- Installed Vulkan SDK 1.4.341.1

The new GPU smoke test constructs an actual HWND and window surface,
initializes the renderer via the plugin ABI, submits real Vulkan draw commands,
presents the swapchain, reads GPU pixels back, recreates the swapchain after
resizing and checks that an unsupported UV test mode fails explicitly.

A separate 1,000-frame Vulkan stress test passed.

The native render pipeline uses dynamic rendering and Synchronization2
supported by Vulkan 1.3+, with a validation-checked GPU device. The command
buffer uses the existing shared backend-neutral Render Graph executor to
record image layout/access transitions and per-pass operations. This is
not mock GPU execution.

### Validation layer

`NORTHSTARCPP_VULKAN_VALIDATION=1` enables
`VK_LAYER_KHRONOS_validation` and a Debug Utils messenger. Validation errors
are tracked and surfaced as a failed renderer draw rather than silently
ignored. The validation-enabled GPU smoke and 1,000-frame timing stress passed
on the current hardware.

### Vulkan GPU timestamp extension

The existing `NorthStarRenderTimingApi` version 1 is now exported by the
Vulkan provider, while external Render ABI 5 remains unchanged. Vulkan query
timestamps are recorded, resolved after GPU queue completion and read through
the same optional timing extension as D3D12.

Actual Vulkan hardware GPU timestamp result, 1,000 frames of the **small
colored-geometry smoke scene**, with the validation layer enabled:

| Metric | Measured GPU duration |
| --- | ---: |
| p50 | 0.0431 ms |
| p95 | 0.0463 ms |
| p99 | 0.0497 ms |

Vulkan currently implements a single opaque geometry pass; deferred, PostFX
and HUD timeline segments remain empty. The values are **not** a Night City
benchmark and must not be compared to D3D12 results from a different scene.

Timing readbacks and queue-idle synchronization are intentionally conservative
for this initial validation implementation. Future multi-frame-in-flight
execution should use per-image semaphores/fences and asynchronous timestamp
readback to reduce CPU stalls.

## 2. Tested functionality and known limitations

| Capability | Status |
| --- | --- |
| Vulkan physical/logical device and graphics queue | Working |
| Win32 surface and swapchain | Working |
| GPU command submission and presentation | Working |
| Shader modules and a basic GPU graphics pipeline | Working |
| Color triangles / cubes and owned geometry uploads | Working |
| Shared Render Graph Sync2 transitions | Working |
| Pixel readback and resize | Working |
| Vulkan validation layer | Working |
| GPU timestamp query extension | Working |
| 1,000-frame hardware stress | Passed |
| Depth buffer / depth-tested opaque meshes | Not yet |
| YTD/YDD/YDR material and model integration | Not yet |
| Full textured skydome | Not yet; `set_sky(non-null)` explicitly fails |
| PBR, normal/metallic/roughness maps, material graph | Not yet |
| Deferred G-buffer and tiled/clustered lighting | Not yet |
| CSM / PCSS / SSAO / HDR / Bloom / Tonemapping / FXAA | Not yet |
| Original HUD, font rendering and 2D compositing | Not yet |
| Vulkan per-frame asynchronous present (multi-flight) | Not yet |
| Native Vulkan transient memory alias allocator | Not yet |
| Real Night City world/project GPU profile | Not tested |

This is a valid, independently loadable GPU renderer baseline and **not** yet
feature-parity replacement for the existing D3D12 provider.

Unsupported features are reported as unsupported rather than silently
pretending that shader/material outputs were rendered.

## 3. Enhanced D3D12 Barriers

The existing D3D12 Enhanced Barrier mapping and capability test were kept
untouched. Testing on current hardware and WARP indicates
`EnhancedBarriersSupported == FALSE`. GPU Enhanced Barrier tests therefore
return the supported CTest **Skip** code, not a false pass.

D3D12 continues to use its tested legacy resource barriers. No unsafe
mixing of Enhanced and Legacy resource-state contracts was introduced.

## 4. Regression and workspace changes

The final targeted suite reported **19/19 passed**:

- Vulkan native GPU, Vulkan validation and 1,000-frame Vulkan stress
- Vulkan hardware GPU profiling, 1,000 frames
- D3D11 and D3D12 material smoke on hardware and WARP
- D3D12 native rendering, HDR/CSM and transient aliasing
- Vulkan shared CPU Render Graph executor/Sync2 tests
- D3D12 transient allocation and texture pool unit tests

There was an intermediate failure in four material-graph tests because the
`northstarcpp_d3d11_material_smoke` executable was stale relative to
already-modified asynchronous shader/PSO source code. Rebuilding that test
target resolved all four failures without undoing any existing material
changes.

Build and regression details:
- `northstarcpp_render_vulkan`: compiled, linked.
- `northstarcpp_render_d3d11`: compiled, linked.
- `northstarcpp_render_d3d12`: compiled, linked.
- `git diff --check`: clean for tracked Vulkan/CMake edits.

## 5. Night City profiling blocker

The following previously referenced project paths were not found in the
connected workspace:

- `C:\Users\Kayla\Documents\NorthStar\Projects\KaylaNightCity`
- `C:\Users\AidenFox\Documents\NorthStarCpp\Projects\KaylaNightCity`
- `C:\Users\AidenFox\Documents\NorthStar\Projects\KaylaNightCity`

The current `NorthStarCpp/Projects` tree contains
`NorthStarFirstPersonPlatform` rather than `KaylaNightCity`.

Therefore **no Night City scene profiling was performed**. The synthetic
smoke scene is useful to validate GPU timing and Vulkan stability but is
not a substitute for real city geometry, texture streaming, NPC physics
and vehicle motion.

## 6. Next milestones

1. Add Vulkan depth/stencil images and correct depth-tested opaque geometry.
2. Implement persistent model/material resource uploads, texture samplers
   and the existing plugin Material API v1/v2.
3. Port GPU-PBR, G-buffer, clustered lighting, CSM/PCSS and HDR/PostFX
   through backend-independent Render Graph pass definitions.
4. Port sky/hud with independent final composite, and retain correct pixel
   readback and MSAA/resize/device-loss behavior.
5. Implement proper per-frame-in-flight synchronization and Vulkan GPU
   transient allocation according to `VkMemoryRequirements`.
6. Run validation-enabled shader/material parity tests against D3D12.
7. Once the actual Night City project becomes accessible, collect at least
   1,000 valid frames of GPU timestamp distributions and CPU/GPU breakdowns
   while measuring asset residency and transport physics.

## 7. Change control

All new work remains in the current NorthStarCpp source tree. The newly built
Vulkan plugin is an experimental optional provider. Existing D3D11/D3D12
production deployment was not modified; `dist` was **not** replaced.

No Git commit and no push were performed. Existing unrelated changes in
material providers, shader caching and other engine modules were preserved.
New Vulkan files are not yet added to Git's index.
