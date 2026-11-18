# NorthStarCpp — Vulkan HDR/PostFX Compositor
Date: 2026-11-17
QA build: `_qa_hdr_postfx_20261117`

## Delivered

### Vulkan frame pipeline (opt-in)
Set `NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1` and
`NORTHSTARCPP_ENABLE_VULKAN_HDR=1` **before** provider initialization.

New GPU order:
1. GBuffer (plus optional half-res SSAO / cascaded shadows).
2. Deferred PBR lighting into a dedicated `VK_FORMAT_R16G16B16A16_SFLOAT`
   scene-color target, without saturation / grading / tonemapping.
3. Authored translucent forward material batches are alpha-blended in
   the **same FP16** scene-color attachment using an HDR-compatible PSO.
4. Four persistent FP16 bloom images, each half of the preceding size.
   First level uses a soft highlight threshold; remaining levels perform
   multi-tap spatial downsampling. The composite samples and blends all
   four levels using fixed normalized weights.
5. Fullscreen HDR→SDR compositor: manual exposure in EV (existing provider
   PostFX ABI v1), ACES-fitted tonemap, contrast, saturation, vignette,
   chromatic aberration in screen pixels, frame-salted film grain and
   display-format-aware gamma encode (UNORM vs SRGB swapchain).
6. Independent `engine.ui` overlay on the final swapchain, then GPU
   readback and presentation. HUD is NOT processed by HDR grading or bloom.

All new persistent textures and descriptor/pipeline layouts are owned by
`Renderer`, created/recreated on swapchain resize and destroyed on shutdown.
No new per-frame image allocations. The neutral default Filmic settings
still produce an ACES display transform when HDR is enabled.

### Implementation files
Added:
- `engine/northstarcpp/modules/render/vulkan/deferred_bloom.frag`
- `engine/northstarcpp/modules/render/vulkan/deferred_composite.frag`
- `engine/northstarcpp/modules/render/vulkan/renderer_hdr_resources.cpp`
- `engine/northstarcpp/modules/render/vulkan/renderer_hdr_pass.cpp`

Updated:
- `deferred_resolve.frag`: distinct `NS_HDR_LINEAR` shader variants,
  removing premature SDR clamping and Filmic during HDR lighting.
- `deferred_spirv.hpp` and `tools/generate_vulkan_deferred_spirv.py`:
  compiled 8 new HDR resolve permutations plus bloom/composite shaders.
- `renderer_device.cpp`: HDR feature gate, framegraph resources and
  order; ensures final UI is after the HDR compositor.
- `renderer_frame.cpp`: extended resource bindings and pass dispatch.
- `renderer_deferred.cpp`: HDR attachment for deferred + forward passes
  and appropriate FP16 graphics pipeline format.
- `renderer_materials.cpp`: translucent forward PSO compatible with FP16
  scene-color target, preserving existing material ABI.
- `renderer_private.hpp`, `renderer_film_look.cpp`, CMake and
  `tests/render_vulkan_smoke.cpp`: lifetime, API, QA and build integration.

### Memory
At 1920×1080, the scene RGBA16F target is approximately 15.82 MiB.
Bloom half/quarter/eighth/sixteenth RGBA16F textures add approximately
5.27 MiB, for roughly **21.1 MiB** extra GPU image data before alignment
and backend bookkeeping. This does not include GBuffer/SSAO/CSM images.

### Verified QA
- `glslc --target-env=vulkan1.3` compiled all GLSL/SPIR-V variants: PASS.
- CMake/Ninja built `render_vulkan.dll`, Vulkan GPU smoke and
  `render_d3d12.dll`: PASS.
- HDR enabled: 8 of 8 hardware shader/rendergraph combinations PASS
  (GBuffer v1/v2 × SSAO 0/1 × CSM 0/1).
- Vulkan smoke asserts PostFX API round-trip of non-neutral Filmic
  settings (including chromatic aberration in HDR), UI red-pixel GPU
  readback despite active grading, depth-tested geometry, resize,
  clear/present and resource recreation: PASS.
- Vulkan HDR + SSAO + CSM + contact shadows: 1000-frame hardware stress,
  GPU profile and validation: 3/3 PASS.
- HDR disabled: legacy SSAO/CSM hardware tests 6/6 PASS.
- D3D12 PostFX and Vulkan device-probe hardware tests 2/2 PASS.
- Render Graph, Vulkan Sync2 and executor tests 3/3 PASS.
- Git diff --check on changed tracked files: PASS.

### Synthetic GPU frame timings (not real game benchmark)
Identical QA settings: GBuffer v2, SSAO ON, CSM ON, 1000 frames:
| | HDR OFF | HDR ON |
| --- | ---: | ---: |
| p50 | 0.1025 ms | 0.2540 ms |
| p95 | 0.1590 ms | 0.2596 ms |
| p99 | 0.1619 ms | 0.2634 ms |

p95 additional cost +0.1006 ms in this small synthetic test. These
values cannot be extrapolated to the full Night City scene and are NOT
evidence of an FPS gain.

## Known limitations and remaining backlog
- **Manual exposure only**: no luminance histogram, temporal eye
  adaptation, local exposure or HDR10/rec.2020 output.
- Bloom is a four-level spatial pyramid with fixed weights/strength;
  bloom blur/upscale and quality controls can be expanded.
- Vulkan forward fallback for textured world surfaces still bypasses the
  opt-in Deferred/HDR path; native world-surface feature parity remains
  separate work.
- Alpha-masked skinned/static shadow casters, point/spot shadows and
  shadow cache are not production-complete.
- Temporal SSAO, TAA, motion vectors, motion blur, DoF, volumetric fog,
  SSR and GPU-driven culling have NOT been implemented by this change.
- Large-scene translucent sorting, sky/exposure visual grading,
  D3D12/Vulkan screenshot matching and City-scale VRAM/p95 tests remain
  required for production acceptance.

## Delivery
Changed local working-tree source only. QA binaries remain in
`_qa_hdr_postfx_20261117`.
Production dist/EXEs/DLLs were not replaced. No commit or push.
Other uncommitted work (UI editor, physics, animation, etc.) preserved.
