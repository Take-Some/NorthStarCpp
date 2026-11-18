# NorthStarCpp — GBuffer parity and temporal-motion foundation

Date: 2026-11-17
Workspace: `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`
QA build: `_qa_hdr_postfx_20261117`
Hardware: NVIDIA Quadro RTX 4000

## Implementation: fixed material classification

The native primitive GBuffer semantics were previously incompatible:

- D3D12 wrote `normal_rough.a=+0.7`, so a cube was interpreted as a
  physically lit PBR surface despite having only baked face tints.
- Vulkan wrote `normal_rough.a=-0.7`, which correctly tagged its cube as
  unlit. Both deferred resolve shaders already recognize the negative
  signed-roughness convention.

Updated `modules/render/d3d12/shader_compiler.cpp` so ordinary primitives
write `-0.7`, exactly like Vulkan. **This changes their intentional visual
classification**: asset-authored PBR materials still use positive roughness.

Identical 496x345 camera/geometry HDR fixture (same first-frame setup):

| Metric | Before | After |
|---|---:|---:|
| D3D12/Vulkan RGB MAE (8-bit) | 17.0818 | **0.04894** |
| D3D12/Vulkan foreground silhouette IoU | 0.8070 | **0.99238** |
| Center RGB on D3D12 | (142,142,161) | **(230,230,237)** |
| Center RGB on Vulkan | (230,230,237) | **(230,230,237)** |

This is an almost pixel-identical result for this **controlled primitive
fixture only**, NOT full material, lighting, transparency, PostFX or
performance equivalence. A few edge pixels still differ.

## Implementation: GBuffer diagnostic parity

Vulkan's primitive MRT path previously wrote the same constant
`(0,1,0)` normal for every face, whereas D3D12 used geometric screen-space
derivatives. Added `vulkan/deferred_primitive.vert`, carrying the
already-existing world-space vertex attribute to the fragment stage.
`deferred_gbuffer.frag` computes the face normal from
`cross(dFdx(vertex_world),dFdy(vertex_world))` with a degenerate fallback.
The Vulkan primitive GBuffer PSO now uses three vertex inputs (offsets
0,16,32), leaving the forward/legacy primitive PSO unchanged.
Regenerated `deferred_spirv.hpp`.

Vulkan's debug modes used to return black for uncovered GBuffer pixels,
while D3D12 retained the HDR sky background. `deferred_resolve.frag`
now preserves sky under the diagnostic overlays except for Coverage,
which intentionally displays zero in uncovered pixels.

After changes, same-camera GPU output:

| GBuffer debug mode | RGB MAE /255 |
|---|---:|
| Albedo | 0.04894 |
| Normal | 0.05727 |
| Roughness | 0.04939 |
| Coverage | 0.11308 |

Added `tests/renderer_gbuffer_parity_gpu.py`, which performs genuine GPU
captures in both native providers for all four modes, and asserts strict
RGB MAE <=0.5 and silhouette IoU >=0.98.
Tightened the existing `tests/renderer_cross_api_fixture_gpu.py`
regression envelope from MAE<=25, IoU>=0.70 to MAE<=0.5, IoU>=0.98.

## Implementation: common temporal reference

Created:
`engine/northstarcpp/modules/render/shared/temporal_motion_vectors.hpp`

Includes:

- Reproducible 8-step centered Halton (2,3) subpixel jitter sequence.
- Resets both current/previous jitter on history invalidation, so a camera
  cut never creates artificial motion.
- Pixel-motion convention `previous_pixel - current_pixel` and pixel
  units for both backends, with separate previous and current world-space
  positions to support rigid-object motion.
- Reference 8-influence native skin palette transforms that match
  `vulkan/material_skin.comp`: row-vector matrix arrangement and
  translation at float elements 12..14.
- Velocity from previous/current skinned world positions, with explicit
  history validity and previous palette requirements.
- Rejects invalid weights, absent palettes, invalid joints and nonfinite
  positions. Does not silently treat missing skeletal motion as zero.

Added `tests/render_temporal_motion_smoke.cpp` with tests of:
static/camera-relative velocity, changing rigid world positions,
subpixel jitter, actual moving bone palette, invalid influences,
missing previous palette, camera teleport invalidation.
Registered as CTest `northstarcpp_render_temporal_motion`.

**Scope limitation:** These are CPU reference math, validation and
contract tests. They are NOT yet a provider GPU Motion Vector Buffer,
nor jittered raster projection, nor temporal anti-aliasing.
`NorthStarSkinnedMaterialBatch` ABI 1 does not provide stable instance
identity or a previous pose palette. Full skinned motion needs an
additive extension carrying stable IDs and prior-pose/world information,
or explicit renderer-owned histories with proven matching IDs. The GPU
velocity buffer must include material/object/skinned motion, not merely
camera reprojection.

## QA

- Vulkan and D3D12 provider build: **PASS**
- HDR GBuffer v1/v2 x SSAO on/off x CSM on/off: **8/8 PASS**
- Vulkan auto exposure/Bloom quality variants 0/1/2/3: **4/4 PASS**
- Device probe D3D12 PostFX and Vulkan HDR world: **2/2 PASS**
- Vulkan and D3D12 3-layer alpha regression: **2/2 PASS**
- Controlled strict RGB/silhouette parity: **1/1 PASS**
- Four-mode GBuffer debug GPU parity: **1/1 PASS**
- Temporal history + motion reference: **2/2 PASS**
- Combined feature matrix: **20/20 PASS**
- Separate Vulkan 1000-frame stress, GPU profile and Validation with
  HDR+Auto Exposure+Bloom High+SSAO+CSM+GBuffer v2: **3/3 PASS**
- All measurements refer to the controlled fixture and this QA build;
  an overall production performance benefit has not been measured.

## Required next steps (NOT implemented)

1. Add additive provider-neutral motion-instance identity and previous
   transforms/bone palettes. Keep original ABI stable.
2. GPU RG16F velocity MRT or full-screen motion-vector pass in BOTH
   providers, with camera + object + skinned motion and validation of
   reprojection direction, near-plane invalidation and disocclusions.
3. Apply matching Halton projection jitter to actual rasterization
   without adding jitter to world-space shadow matrices or HUD.
4. Implement TAA pass in **linear FP16 scene** after opaque and
   translucent geometry, BEFORE Bloom/Exposure/Filmic. Use ping-pong
   history, neighborhood luminance/color clamp, depth/normal
   disocclusion rejection, velocity-aware weighting and reset upon
   teleports, FOV changes, resize, camera-mode changes and asset reload.
5. Measure GPU temporal stability in moving/skinned scenes, ghosting,
   jitter, GPU frame time and memory. Verify both D3D12/Vulkan and
   preserve readback/picking/ui order.
6. Separately verify physically lit YMAT PBR materials, tangent-space
   normals, direct/dynamic lights, transparent self-intersections/OIT,
   volumetric fog, SSR, Motion Blur and DoF.

No production DLL/EXE replacement. No Git commit or push.
Concurrent unrelated source modifications preserved.
