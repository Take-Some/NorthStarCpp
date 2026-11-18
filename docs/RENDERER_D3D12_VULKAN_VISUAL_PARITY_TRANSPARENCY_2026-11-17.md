# NorthStarCpp — D3D12/Vulkan HDR Visual Parity & Multi-layer Transparency

**Date:** 2026-11-17
**Repo:** `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`
**Isolated hardware QA:** `_qa_hdr_postfx_20261117`
**Measured GPU:** NVIDIA Quadro RTX 4000

## Status

This pass implements a meaningful subset of the priority: first-frame
exposure/color parity, more consistent native cube geometry, both-backend
depth-sorted alpha blending, and an explicit automated cross-backend GPU
image regression. **Full pixel/lighting parity is NOT complete.**
Motion vectors/TAA, motion blur, DoF, volumetric fog and SSR were NOT
implemented in this iteration; TAA should follow closure of color parity.

## HDR visual parity fixes

D3D12 computes a current-frame HDR exposure value from a GPU metering pass
on the first rendered frame. Previously Vulkan started at EV=0 and only
used the GPU meter on the subsequent frame. This misaligned exposure
strongly, particularly in a dark synthetic scene.

Vulkan changes:
1. 8x8 stratified scene luminance meter matching the D3D12 spatial sample
   pattern, log2-luminance stored as 8x8 RG16F.
2. Composite descriptor binding 5 now samples this meter on frame 0.
   Render Graph declares the texture as ShaderRead, including a Sync2
   transition from the transfer-source stage.
3. Subsequent frames use the previous-frame readback plus exponential EV
   adaptation, preserving the asynchronous CPU/GPU execution pattern.
4. Auto EV clamp [-2,+2], matching D3D12 exposure multipliers
   [0.25,4.0]. The manual exposure remains independent EV compensation.
5. Display sRGB conversion now uses the exact piecewise IEC sRGB curve
   rather than the approximate 1/2.2 power.
6. Vulkan default clear/sky color now matches D3D12 linear defaults
   (0.055,0.075,0.105,1.0) instead of (0.037,0.055,0.09,1.0).

**Important measurement caveat:** earlier comparisons of the FIRST default
`--full` GPU probe frame captured D3D12 in third-person orbit view but
Vulkan in first-person view. Those image deltas are NOT valid measures of
same-camera visual parity, even though the 70.601 -> 15.934 and later
11.972/255 reductions were genuine for that test configuration.

A corrected controlled `--parity` fixture now forces:
- camera position (0,0,0), yaw/pitch/roll 0,
- first-person override, FOV 70, near 0.1, far 250,
- identical native cube and triangle GPU input, 496x345 pixel readback.

Native D3D12 `geometry::cube` uses cube position at the BASE of the cube
(y offset half size), row-major rotation, and six authored face brightness
coefficients (.74,1,.60,.82,.95,.68). Vulkan originally interpreted
`cube.y` as center, used a transposed rotation, and omitted face shading.
The standard Vulkan cube path was aligned with D3D12. **BCn textured
world-surface cubes retain their existing center-origin and legacy
rotation semantics** until an explicit versioned asset migration; this
restored the native BC1/BC5 GPU selector test after a first attempted
unconditional change caused a regression.

### Controlled GPU fixture results

| Metric | Before matching cube transform | Current controlled result |
|---|---:|---:|
| Foreground silhouette IoU | 0.6476 | **0.8070** |
| Missed expected D3D foreground pixels | 1774 | **20** |
| RGB MAE vs D3D12 | 13.8174/255 | **17.0818/255** |
| RGB PSNR | not recorded | **20.103 dB** |
| RGB RMSE | not recorded | **25.1994/255** |

This is a tradeoff: geometry/silhouette got more accurate but pixel RGB
remains different. Foreground colors are still too bright in parts of the
Vulkan image, and some extra silhouette coverage remains. **We have
NOT achieved full D3D12/Vulkan visual parity.** Root causes require BRDF,
primitive unlit routing, color-space staging, and sky/tonemap fixture
comparisons at intermediate render targets, not arbitrary tuning to a
single screenshot.

`tests/renderer_cross_api_fixture_gpu.py` runs the two providers using
the controlled fixture, checks for real foreground pixels, checks
MAE <=25 and silhouette IoU >=.70 as a **regression envelope only**.
A PASS does NOT imply visual parity. To inspect pixel differences:
`tools/dev/compare_renderer_ppm.py`.

Generated diagnostics:
- `docs/qa_renderer_parity_20261117/fixture_d3d12.ppm`
- `docs/qa_renderer_parity_20261117/fixture_vulkan_face_match.ppm`
- `docs/qa_renderer_parity_20261117/controlled_fixture/comparison.json`
- `docs/qa_renderer_parity_20261117/controlled_fixture/side_by_side.png`
- `docs/qa_renderer_parity_20261117/controlled_fixture/absolute_difference_x3.png`

## Multi-layer transparency — Vulkan and D3D12

Both providers now **globally stable-sort translucent triangles**
by camera-relative depth, far to near. This replaces material-submission
order (unsuitable for SRC_ALPHA/ONE_MINUS_SRC_ALPHA blending).
Unlit and authored material alpha blend semantics are preserved,
transparent depth writes remain off, opaque materials remain before
transparent batches, and the native UI still composites after Filmic.
Adjacent material/texture PSO runs are coalesced without reordering
sorted triangles. Vulkan uses retained scratch vectors across frames to
reduce transient allocations; D3D12 maintains its own triangle scratch.

Testing:
- 3 overlapping colored translucent quads at different z depths, rendered
  in two separate processes with reversed material submission order.
- GPU backbuffer readbacks captured in binary PPM (`mode=10`).
- Vulkan earlier measured maximum channel delta 1 LSB and mean RGB
  absolute delta ~0.0543/255 in the manually captured pair.
- D3D12 warmed the asynchronously compiled material graph PSOs before
  image capture to prevent false positives from empty sky-only frames;
  measured max channel delta 1 and mean ~0.0296/255 in warm captures.
- Automated tests explicitly reject sky-only/empty captures and tolerate
  up to 1 LSB rounding per channel.
- `northstarcpp_vulkan_transparency_hardware` PASS
- `northstarcpp_d3d12_transparency_hardware` PASS

**Limitation:** triangle-centroid sorting is not true order-independent
transparency. Intersecting polygons, cyclic overlaps, particles, and
deformable translucent meshes still warrant weighted blended OIT,
per-pixel linked lists, depth peeling or other explicit implementations.
Whole-scene large material counts require a CPU sorting cost profile.

## Source changes

- `engine/northstarcpp/modules/render/vulkan/deferred_luminance.frag`
- `engine/northstarcpp/modules/render/vulkan/deferred_composite.frag`
- `engine/northstarcpp/modules/render/vulkan/deferred_spirv.hpp` (generated)
- `engine/northstarcpp/modules/render/vulkan/renderer_hdr_exposure.cpp`
- `engine/northstarcpp/modules/render/vulkan/renderer_hdr_resources.cpp`
- `engine/northstarcpp/modules/render/vulkan/renderer_hdr_pass.cpp`
- `engine/northstarcpp/modules/render/vulkan/renderer_hdr_forward_graph.cpp`
- `engine/northstarcpp/modules/render/vulkan/renderer_device.cpp`
- `engine/northstarcpp/modules/render/vulkan/renderer_private.hpp`
- `engine/northstarcpp/modules/render/vulkan/renderer_frame.cpp`
- `engine/northstarcpp/modules/render/vulkan/renderer_materials.cpp`
- `engine/northstarcpp/modules/render/d3d12/renderer_frame.cpp`
- `engine/northstarcpp/modules/render/d3d12/renderer_private.hpp`
- `engine/northstarcpp/modules/render/d3d12/renderer_pass_postfx.cpp`
- `engine/northstarcpp/modules/render/d3d12/renderer_pass_present.cpp`
- `engine/northstarcpp/modules/render/shared/gpu_capture_ppm.hpp`
- `tests/render_provider_device_probe.cpp`
- `tests/vulkan_transparency_layers_gpu.py`
- `tests/renderer_cross_api_fixture_gpu.py`
- `engine/northstarcpp/CMakeLists.txt`

## Next priorities (not done in this change)

P0: resolve D3D12 vs Vulkan foreground RGB discrepancy using matched
raw GBuffer/albedo/HDR Scene Color/tonemapped readbacks and reference
material fixtures, without changing scene semantics or breaking BCn.

P0: further alpha tests with intersecting polygons and particle-like
overlaps; decide weighted OIT versus sorted alpha.

P1: add GPU velocity buffer (including object/skinned motion), jittered
projection, history ping-pong, neighborhood clamping, disocclusion
rejection, camera teleport/resize/FOV reset and TAA/temporal sharpening
for both render providers using the existing backend-neutral
`TemporalHistoryTracker` from `shared/temporal_history.hpp`.
Motion blur should consume the velocity buffer after initial TAA.

P1: DoF, volumetric fog, SSR remain pending.

No production DLL/EXE replacement. No Git commit or push, and unrelated
working tree changes preserved.


## Final GPU validation incident and fix

The first full 1000-frame Vulkan test with all features including automatic
exposure enabled correctly detected a **Render Graph image-ID mismatch**.
Deferred resource creation had this exact order:

`FP16 scene -> 8x8 exposure meter -> bloom 0 -> bloom 1 -> bloom 2 -> bloom 3`

But `renderer_frame.cpp` incorrectly bound GPU images as:

`FP16 scene -> bloom 0 -> bloom 1 -> bloom 2 -> bloom 3 -> exposure meter`

Vulkan Validation therefore reported illegal `TRANSFER_SRC_OPTIMAL`
transitions for a non-transfer color target, descriptor image-layout errors
and actual src image copy layout mismatches. The fix now makes the binding
table follow the Graph resource creation order, including the optional
Auto Exposure slot. The forward HDR graph uses its own distinct resource
order and remains unchanged.

**Verified AFTER this fix** with Deferred + HDR + Auto Exposure + Bloom High
+ SSAO + CSM + GBuffer v2:
- `northstarcpp_vulkan_stress_hardware`: PASS, 1000 frames.
- `northstarcpp_vulkan_gpu_profile_hardware`: PASS.
- `northstarcpp_vulkan_validation_hardware`: PASS, no GPU layout errors.

Re-captured the controlled first-frame GPU comparison after this fix:
`docs/qa_renderer_parity_20261117/fixture_vulkan_validation_fixed.ppm`.
MAE 17.0818/255 and foreground IoU 0.8070 remained unchanged.
Refreshed `controlled_fixture/comparison.json`,
`side_by_side.png`, and `absolute_difference_x3.png`.
These remain *regression diagnostic* assets, not proof of true parity.

### Additional implementation and QA notes
- Standard Vulkan cube transforms were brought closer to D3D12 semantics
  (base-origin, rotation order, face shade). The existing BCn world-surface
  asset representation uses the historical center-origin/column-major path.
  A blanket transform change initially caused BC1/BC5 selector regressions;
  this was isolated to keep native BCn tests passing.
- Vulkan translucent triangle sorting scratch vectors are now renderer-owned
  and reused between frames, rather than allocated each draw.
- Both D3D12 and Vulkan layered transparency CTests PASS after the final
  asset transform compatibility fix.
- `northstarcpp_device_probe_vulkan_hdr_world_hardware`: PASS.
- `northstarcpp_renderer_parity_fixture_hardware`: PASS under its
  deliberately loose regression guard (MAE<=25, IoU>=0.70).
- Full D3D12/Vulkan color equivalence is still **NOT achieved**.
