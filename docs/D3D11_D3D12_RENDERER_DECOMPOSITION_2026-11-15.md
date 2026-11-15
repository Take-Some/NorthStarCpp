# NorthStarCpp Renderer — D3D11/D3D12 Monolith Decomposition

Date: 2026-11-15

## Goal and non-goals

Split two growing renderer providers into comprehensible backend subsystems
without discarding existing code, changing the externally visible C plugin
interfaces, or mixing the refactor with changes to rendering algorithms.
Preserve the current render ABI 5 and the D3D12 1.6.0 functionality
(CSM/PSSM, PCSS, HDR, bloom, GPU timestamps, materials and dynamic lights).

This is **phase 1 of the structural decomposition**:
- Shader compilation and CPU geometry generators are true, independently
  compiled C++ translation units.
- Renderer instance methods remain inside the original `Renderer` class but
  are organized in *included implementation fragments* (`.inc`).
  This maintains access to the current strongly coupled GPU state.
- The `.inc` files are declared `HEADER_FILE_ONLY` in CMake so that
  IDEs show the subsystem tree without compiling these fragments twice.
- A later refactor should extract a private Renderer state header/PImpl and
  compile render-pass implementations in dedicated `.cpp` files. The
  existing included fragments **are not yet independent object files**.

## Before and after

| Provider entry point | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| `modules/render/d3d11/d3d11.cpp` | 1,880 lines | 412 lines | ~78% |
| `modules/render/d3d12/d3d12.cpp` | 2,575 lines | 343 lines | ~87% |

These figures describe **entry-point source size**, not binary size,
GPU memory use, execution speed, or total source-code deletion. Code was
moved into the files below.

### Direct3D 11

```text
modules/render/d3d11/
  d3d11.cpp                Entry point, Renderer fields, plugin ABI exports
  shader_compiler.hpp      Private HLSL compiler interface (ID3DBlob**)
  shader_compiler.cpp      Shader strings and independent compilation
  renderer_geometry.hpp   CPU vertex layout and primitive helper interface
  renderer_geometry.cpp   Cube mesh CPU generation
  renderer_device.inc     Device, swapchain, PSOs and initialization
  renderer_resources.inc  Resize, readback and resource lifetime
  renderer_hud.inc        FPS UI and native overlay
  renderer_sky.inc        Skydome
  renderer_characters.inc Character vertex/pose, skinning and drawing
  renderer_materials.inc  Authored PBR, material variants and textures
  renderer_surfaces.inc   Native triangles and world surfaces
  renderer_frame.inc      D3D11 frame sequencing and present
```

D3D11's original local COM wrapper is retained. The compiled shader
interface accepts `ID3DBlob**` at its boundary; the existing wrapper
supplies `Com<ID3DBlob>::put()` at all eight shader compilation call sites.
This keeps COM object ownership and creation semantics unchanged.

### Direct3D 12

```text
modules/render/d3d12/
  d3d12.cpp                Entry point, Renderer fields, plugin ABI exports
  shader_compiler.hpp      Private compiled shader interface (WRL ComPtr)
  shader_compiler.cpp      Scene, material, deferred, HDR, bloom, FXAA,
                           shadow and sky HLSL compilation
  renderer_geometry.hpp   Native CPU vertex layout and geometry interface
  renderer_geometry.cpp   Native HUD and cube geometry
  renderer_device.inc     D3D12 device, root signatures, PSOs and setup
  renderer_resources.inc  GPU resources, resize and tiled-light buffers
  renderer_materials.inc  Texture residency, material variants and batches
  renderer_scene.inc      Scene/skydome extraction
  renderer_diagnostics.inc GPU timestamps, memory stats, pixel readback
  renderer_frame.inc      Command-list recording, shadows, G-buffer,
                           HDR postprocessing, HUD and Present
```

The heavy Deferred/CSM/HDR shader-source headers are now included in
`shader_compiler.cpp`, not in the main D3D12 provider TU.
This reduces unrelated shader recompilation when editing the renderer
entry point. Shader compiler and CPU geometry are truly separate objects.

## Build and compatibility

Updated `engine/northstarcpp/CMakeLists.txt`:
- Both providers compile the new `shader_compiler.cpp` and
  `renderer_geometry.cpp` as independent translation units.
- Include-only renderer fragments appear as IDE sources but are marked
  `HEADER_FILE_ONLY`.
- Original target names, output DLLs, dependencies and plugin ABI
  registration remain unchanged.

Rebuilt and linked successfully:
- `northstarcpp_render_d3d11`
- `northstarcpp_render_d3d12`

All changes are in the existing NorthStarCpp source tree, and all pre-existing
uncommitted modifications are preserved. No Git commit or push.

## Functional QA

Isolated CMake/Ninja Release build:
`out/zf_postfx_cmake_qa`

Twelve selected tests passed after the geometry and shader-compiler split:
- `northstarcpp_d3d11_material_warp`
- `northstarcpp_d3d11_material_hardware`
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

`git diff --check` passed for the edited tracked source/CMake files.

### Stress and performance caveat

The restructured D3D12 provider passed a 1000-frame WARP stress test.
CPU+Present timings were highly variable between runs:

| Test candidate | p50 | p95 | p99 |
| --- | ---: | ---: | ---: |
| Restructured QA, run A | 62.276 ms | 284.355 ms | 344.200 ms |
| Restructured QA, run B (final) | 65.037 ms | 156.263 ms | 199.907 ms |
| Previously saved 1.6.0 QA DLL, follow-up run | 39.050 ms | 147.316 ms | 189.198 ms |

The comparison is **not a controlled same-source A/B performance
experiment**: the saved DLL was built earlier, and background system
load, run order and scene state were not normalized. WARP
CPU+Present timings include vsync and are not GPU-only times.
Do not claim a speedup. A repeatable isolated CPU/GPU profiler pass and
a paired A/B run under matching conditions are required to confirm the
effect, if any, on frame time.

## Follow-up technical work

1. **Renderer private state / PImpl:** introduce a private state header,
   resource ownership and subsystem interfaces, then migrate the
   included methods to independent `.cpp` files one subsystem at a time.
2. **D3D12 pass breakup:** extract shadow, opaque/deferred, material,
   postprocessing and HUD command recording from the 664-line frame
   function into Render Graph pass implementations with explicit input
   and output resource contracts.
3. **D3D11 material/character services:** extract texture lifetime,
   skinned meshes, shader variants and batching behind typed interfaces.
4. **Device lifecycle:** isolate resize, fence/allocator, descriptor and
   readback management; avoid repeated resource creation and ownership
   ambiguity.
5. **Cross-provider utilities:** consolidate CPU-side geometry, camera
   payload validation, cache policies and error reporting where semantics
   genuinely match, without merging backend-specific resource APIs.
6. **Performance acceptance:** re-run the same workload on native GPU
   with independent timestamps per pass and 1000-frame p50/p95/p99
   measurements, including cold/warm runs and resize/scene stress tests.

## Deployment status

The test DLLs in `out/zf_postfx_cmake_qa` were built as a QA-only result.
The installed production `dist` was not overwritten or swapped.
No commit or push was performed.

---

## Phase 2: Independent renderer state and Render Graph GPU passes — 2026-11-15

**Status: implemented, compiled and tested.** This section supersedes the earlier phase-1 limitation that renderer methods remained in included implementation fragments. The previous sections are retained as a historical record.

### D3D11 architecture

The D3D11 provider entry point is now 208 lines. A private C++ header (renderer_private.hpp, 221 lines) contains the internal RendererState, COM ownership and material/resource types. All major services are independently compiled:

- renderer_device.cpp — device and swapchain configuration
- renderer_resources.cpp — resize and readback
- renderer_hud.cpp — HUD
- renderer_sky.cpp — skydome
- renderer_characters.cpp — character geometry and skinning
- renderer_materials.cpp — PBR materials and variants
- renderer_surfaces.cpp — world surface data and textures
- renderer_frame.cpp — frame sequencing and presentation
- shader_compiler.cpp and renderer_geometry.cpp — existing independent units

The original local Com template and ID3DBlob shader compiler ownership contract remain unchanged.

### D3D12 architecture

The D3D12 ABI entry point is 144 lines, with a 249-line renderer_private.hpp and a 217-line renderer_frame.cpp. Device, GPU resource management, materials, scene loading and diagnostics are separate translation units. The GPU frame is split into five additional independently compiled passes:

| Stage | Source file |
| --- | --- |
| Cascaded shadows | renderer_pass_shadow.cpp |
| Opaque G-buffer | renderer_pass_gbuffer.cpp |
| Deferred lighting | renderer_pass_deferred.cpp |
| HDR, Bloom, Exposure, FXAA and transparency | renderer_pass_postfx.cpp |
| HUD, GPU readback and Present | renderer_pass_present.cpp |

A stack-owned FramePassContext passes prepared camera matrices, CSM projections, render-target handles and geometry ranges to these functions synchronously. It does not retain stack references across frames.

The private renderer_pass_graph.hpp now contains the single source of truth for the five-stage scheduler, including the FrameStage enum. RendererState::draw compiles this existing backend-neutral RenderGraph into a scheduled plan and dispatches the separate GPU pass implementations in graph order. HUD/Present correctly declares a read/write dependency on the output resource.

**Important boundary:** dependency scheduling uses the real Render Graph, but explicit D3D12 GPU barriers, descriptor heap transitions and resource formats are still handled by individual passes. Automatic barrier lowering, lifetime-based transient memory aliasing, asynchronous execution and the Vulkan backend executor remain future tasks. Do not report full GPU Render Graph resource management as complete.

### Removed intermediate generated fragments

Eight obsolete D3D11 and six obsolete D3D12 renderer_*.inc files were removed. Each was identified as a generated phase-1 include and verified to have no remaining source references before deletion. These intermediate fragments were replaced by independently compiled .cpp methods; no unrelated files were deleted.

### Build and acceptance

Isolated Release CMake/Ninja build: out/renderer-state-qa-20261115.

Both render_d3d11.dll and render_d3d12.dll built and linked successfully. A single final CTest run passed **13/13 selected tests**:

- northstarcpp_d3d11_material_warp
- northstarcpp_d3d11_material_hardware
- northstarcpp_d3d12_material_warp
- northstarcpp_d3d12_material_hardware
- northstarcpp_d3d12_warp
- northstarcpp_d3d12_hardware
- northstarcpp_d3d12_csm_hdr_warp
- northstarcpp_d3d12_csm_hdr_hardware
- northstarcpp_render_graph
- northstarcpp_d3d12_pass_graph
- northstarcpp_deferred_math
- northstarcpp_render_tiled_lights
- northstarcpp_render_cascaded_shadows

The newly registered northstarcpp_d3d12_pass_graph smoke test verifies stage ordering, pass dependencies, resource lifetimes and swapchain read/write hazards from the actual shared graph description.

A separate 1000-frame WARP stress test passed. This run measured CPU + Present frame times, including synchronization, rather than pure GPU frame time:

| Percentile | CPU + Present |
| --- | ---: |
| p50 | 16.659 ms |
| p95 | 17.699 ms |
| p99 | 31.411 ms |

These are test-scene QA results, not a controlled A/B benchmark or Night City frame time. They do not establish 60/120 FPS under production workloads.

Git diff --check was clean on edited tracked renderer/CMake files. All new .cpp and private-header files are currently untracked and must be included when a future commit is explicitly requested. No production dist replacement, commit, or push was performed, and unrelated workspace edits were preserved.

### Remaining work

- Narrow the private headers into per-subsystem resource interfaces; RendererState is private to the provider but not a fully opaque PImpl boundary yet.
- Add D3D12 Render Graph barrier emission from declared resource usage, resource lifetime analysis and optional transient target reuse.
- Apply the same pass scheduling to Vulkan, with backend-specific synchronization.
- Add device lost/recreation, window-resize, alpha-masked material and streaming regression coverage.
- Run controlled hardware GPU timestamp profiling and game-level Editor/Player visual verification.
