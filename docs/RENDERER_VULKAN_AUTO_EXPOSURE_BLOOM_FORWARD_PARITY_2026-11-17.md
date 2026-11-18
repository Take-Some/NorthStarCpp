# NorthStarCpp Vulkan: Auto Exposure, Bloom Quality, HDR Forward World, Provider QA

Date: 2026-11-17
Source workspace: C:\Users\Kayla\Documents\NorthStar\NorthStarCpp
Isolated QA build: `_qa_hdr_postfx_20261117`

## Implemented

### HDR auto exposure by actual scene luminance (opt-in)
- GPU fragment shader `deferred_luminance.frag` samples 256 scene positions
  and renders their log2 luminance into an 8x8 `VK_FORMAT_R16G16_SFLOAT`
  attachment. Metering happens AFTER transparent geometry.
- Vulkan Render Graph transitions that image to CopySource, then copies 256
  bytes into a coherent, host-visible buffer.
- `Renderer::draw` already waits for the previous frame completion fence;
  `Renderer::collect_hdr_exposure` maps **the previous frame's** metering
  result at that point, not a new synchronous GPU readback. Invalid or
  non-finite samples are rejected.
- CPU computes geometric mean luminance (mean in log2 space), targets
  18% middle gray, clamps auto EV to [-5,+5], then smooths EV exponentially
  using elapsed wall-clock delta and separately configurable adaptation
  speeds for brighter/darker exposure.
- Existing Film Look manual EV is an additive exposure compensation; no ABI
  changes to `NorthStarRenderPostFxSettings`.
- Environment:
  `NORTHSTARCPP_VULKAN_AUTO_EXPOSURE=1`,
  `NORTHSTARCPP_VULKAN_EXPOSURE_SPEED_UP=1.5`,
  `NORTHSTARCPP_VULKAN_EXPOSURE_SPEED_DOWN=2.5`.
  Requires `NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1` and
  `NORTHSTARCPP_ENABLE_VULKAN_HDR=1`.
- Readback scratch is 256 bytes, not a full-screen frame or a persistent
  high-resolution CPU image. Metering adds a full-screen sampling pass
  (256 samples total), a small image-to-buffer copy and CPU adaptation.
- **Limitations:** spatial 8x8 meter rather than a luminance histogram;
  no spot-meter masks, scene brightness percentile exclusion, local
  exposure or HDR10 output.

### Configurable Bloom performance and appearance
- `NORTHSTARCPP_VULKAN_BLOOM_QUALITY`: 0=disabled, 1=low
  (2 levels, 1 tap/pixel), 2=medium (3 levels, 5 cross taps),
  3=high (4 levels, 9 weighted taps), default 3.
- **Actual GPU passes are omitted** when quality is lower; there is no
  empty placeholder pass pretending to reduce GPU cost.
- `NORTHSTARCPP_VULKAN_BLOOM_STRENGTH`: clamped [0,2], default 0.18.
- `NORTHSTARCPP_VULKAN_BLOOM_THRESHOLD`: [0.05,16], default 1.05.
- `NORTHSTARCPP_VULKAN_BLOOM_KNEE`: [0.01,4], default 0.50.
- Compositor samples only initialized bloom levels and normalizes active
  pyramid weights. Bloom=0 skips sampling entirely.
- Intermediate GPU images remain allocated until resize/shutdown for
  simple descriptor and lifetime management; lower quality saves bandwidth
  and GPU work, but **not** all bloom allocation memory. No shader ABI
  changes, no duplicate PSO per quality preset.

### HDR support for native BCn forward world surfaces
- Previously a textured BCn world surface forced the entire Vulkan frame
  back to the old SDR forward graph and bypassed the FP16 compositor.
- Dedicated `renderer_hdr_forward_graph.cpp` now implements
  `FP16 forward geometry -> optional luminance meter -> Bloom Pyramid
  -> ACES/Filmic compositor -> engine.ui -> GPU readback -> present`.
- New FP16-compatible primitive and native world-surface PSO variants;
  authored material and GPU-skinned forward PSOs use the same FP16 attachment
  format when HDR is enabled. Opaque/masked/transparent batching remains.
- Existing opt-in Deferred GBuffer+SSAO+CSM path is unchanged for scenes
  without native BCn forward-only surfaces.
- **Limitation:** forward-only BCn path still does not receive all
  Deferred-specific GBuffer SSAO/CSM contributions; only the HDR scene
  composition is unified here.

### GPU pixel capture & cross-provider visual QA
- New optional `NORTHSTARCPP_GPU_CAPTURE_PPM=<absolute .ppm file>`
  captures the FIRST verify-mode-1 GPU readback from either D3D12 or
  Vulkan, using their actual native backbuffer byte ordering.
- New dependency-free `tools/dev/compare_renderer_ppm.py` validates
  same resolution, computes MAE/RMSE/PSNR/percentiles and writes a
  side-by-side PNG, 3x amplified difference PNG and JSON metrics.
- Identical **GPU provider probe program**, same initial geometry/input,
  496x345 frame, NVIDIA Quadro RTX 4000. D3D12 enabled its HDR/Bloom
  options; Vulkan enabled HDR/AutoExposure/Bloom High/GBuffer v2/SSAO/CSM.
- Both provider `--full` tests passed and generated images.
- **Measured visual discrepancy (NOT parity):**
  MAE=70.601/255; RMSE=72.654/255; PSNR=10.9056 dB;
  p95 absolute channel error=111, p99=141.
  Mean RGB D3D12=(120.068,140.137,162.741);
  Vulkan=(58.181,73.309,98.794).
  The Vulkan scene/background is considerably darker.
  This is an actionable HDR sky/tonemap/color-space parity failure,
  not a screenshot-equivalence pass. The test cannot establish full
  Night City parity or correct translucent sorting.

Artifacts (all under `docs/qa_renderer_parity_20261117/`):
- `d3d12.ppm`, `vulkan.ppm`
- `side_by_side.png`
- `absolute_difference_x3.png`
- `comparison.json`

## GPU hardware QA
- CMake/Ninja Vulkan renderer and D3D12 renderer: PASS.
- GLSL/SPIR-V generator including new HDR metering shader: PASS.
- Auto Exposure + Bloom quality 0/1/2/3 GPU CTest: **4/4 PASS**.
- Full Vulkan HDR BCn/world-texture probe with auto exposure: **PASS**,
  including `surfaces_gpu_readback=true`, Material Graph,
  `graph_gpu_readback=true`, animated model GPU skinning, GPU pick.
- Existing HDR GBuffer 1/2 x SSAO 0/1 x CSM 0/1: previously 8/8 PASS.
- Existing D3D12 full PostFX probe: PASS. Renderer plugin ABI unchanged.
- Two provider PPM frame-capture probes: 2/2 PASS.

Synthetic GPU timing, 1000 frames (Vulkan Deferred HDR, SSAO, CSM,
GBuffer v2, automatic exposure enabled):
| Bloom | p50 ms | p95 ms | p99 ms |
| --- | ---: | ---: | ---: |
| OFF (quality 0) | 0.2993 | 0.3059 | 0.3084 |
| HIGH (quality 3) | 0.4628 | 0.4729 | 2.2592 |

The high-quality p99 result includes an outlier and is NOT a stable
performance estimate. Difference at p95=+0.1670 ms. This is a small
synthetic scene; no measured improvement for Night City FPS is claimed.

## Source changes for this iteration
- Added `deferred_luminance.frag`, `renderer_hdr_exposure.cpp`,
  `renderer_hdr_forward_graph.cpp`,
  `shared/gpu_capture_ppm.hpp`,
  `tools/dev/compare_renderer_ppm.py`.
- Updated `renderer_private.hpp`, `renderer_device.cpp`,
  `renderer_frame.cpp`, `renderer_hdr_pass.cpp`,
  `renderer_hdr_resources.cpp`, `renderer_materials.cpp`,
  `renderer_surfaces.cpp`, `deferred_bloom.frag`,
  `deferred_composite.frag`,
  `renderer_diagnostics.cpp` (D3D12; opt-in capture only),
  CMake, shader-generation script, generated `deferred_spirv.hpp`.

## Remaining acceptance blockers / roadmap
- Resolve the measured D3D12/Vulkan exposure, sky, color-space and
  Filmic differences on a **matched exposure** configuration; create
  a city-scale screenshot and BRDF fixture suite before claiming parity.
- Sorting and physically correct depth of multiple overlapping
  translucent materials must be tested under HDR bloom.
- BCn world-forward GBuffer parity for SSAO and CSM, alpha-mask caster
  fidelity, directional shadows for translucent objects.
- Production auto exposure should eventually move to histogram/compute
  with configurable percentile rejection and fully GPU-resident history;
  current 8x8 log-luma + CPU previous-frame filtered EV is practical
  but not equivalent to Unreal's full exposure pipeline.
- No TAA, motion vectors, motion blur, DoF, volumetric fog or SSR in
  this iteration.

No production `dist` replacement, no commit/push, other unrelated
dirty working-tree sources preserved.
