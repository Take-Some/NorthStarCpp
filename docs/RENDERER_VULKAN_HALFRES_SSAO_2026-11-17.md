# NorthStarCpp — Vulkan Half-Resolution SSAO and Bilateral Resolve
Date: 2026-11-17
Workspace: C:\Users\Kayla\Documents\NorthStar\NorthStarCpp

## Scope and status
Implemented and verified a distinct half-resolution ambient-occlusion pass
for the experimental Vulkan Deferred renderer. This closes one P0 item,
**not** full parity with D3D12 or Unreal Engine 5.

The other P0 items — full Vulkan CSM shadow maps and a full-scene
HDR/Bloom/Filmic composite after translucency — remain open. P1 point/spot
shadow caching, TAA/motion vectors/motion blur, DoF, volumetric fog, SSR,
GPU-driven culling and complete GPU pipeline caching remain open.

## Implementation
- `modules/render/vulkan/deferred_ssao.frag`: 8 depth samples per
  half-resolution pixel, using the existing world-space AO radius and
  surface-angle bias; stores occlusion and original depth in `RG16F`.
- `modules/render/vulkan/renderer_ssao.cpp`: independent renderer component
  allocates RG16F color/sample image and view, records a standalone GPU pass,
  and releases resources on swapchain rebuild / shutdown.
- `modules/render/vulkan/deferred_resolve.frag`: 5-tap, depth/normal-weighted
  bilateral reconstruction. Occlusion attenuates ambient only, and does
  not attenuate local lights, direct sun or emissive surfaces. Near-zero
  normals are safely rejected, and normal exponent 8 is implemented with
  multiplies rather than general power.
- `modules/render/vulkan/renderer_{deferred,device,frame,private}.*`:
  descriptor binding 7 only when SSAO enabled, two shader variants each
  for GBuffer v1/v2, AO pipeline, separate typed Render Graph pass and
  explicit sampled-image lifetime. Existing provider ABI remains unchanged.
- `tools/generate_vulkan_deferred_spirv.py`:
  generates both SSAO resolve variants plus the new AO fragment SPIR-V.
- `engine/northstarcpp/CMakeLists.txt`: registers the new source and
  hardware tests for GBuffer v1 and v2.

## Resource / quality constraints
- SSAO remains opt-in via `NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1` and
  `NORTHSTARCPP_ENABLE_VULKAN_SSAO=1` before renderer construction.
- Additional memory: RG16F half resolution = 4 bytes * ceil(width/2)
  * ceil(height/2); approximately 2 MiB for 1920x1080, excluding
  allocation/alignment overhead. No persistent CPU staging buffer.
- Temporal accumulation / reprojection is NOT implemented. This is
  a spatial bilateral filter with approximate depth/normal rejection,
  not a complete GTAO/HBAO system.
- Sampling from the same GBuffer depth and normal sources avoids changing
  NEF8/ YMAT, world material formats or the render provider ABI.
- The world-surface forward fallback remains unchanged; SSAO is only in
  the Vulkan deferred path. The D3D12 inline AO path is unchanged.
- Framegraph barriers (GBuffer -> AO -> resolve -> forward -> UI -> present)
  are owned by the existing Vulkan Sync2 render-graph executor.
- Isolated GPU test passes do not validate all alpha/skin geometry
  or establish frame-time improvements in a populated Night City scene.

## Verification
Dedicated QA build: `_qa_renderer_20261117`.
- glslc generated `deferred_spirv.hpp` successfully.
- CMake/Ninja: Vulkan GPU DLL and smoke executable PASS.
- CMake/Ninja: D3D12 GPU DLL PASS.
- CTest hardware: `northstarcpp_vulkan_ssao_gbuffer_1_hardware` PASS.
- CTest hardware: `northstarcpp_vulkan_ssao_gbuffer_2_hardware` PASS.
- CTest hardware: Vulkan GBuffer v2 deferred + SSAO + contact shadows:
  hardware, stress 1000 frames, profile 1000 frames, **3/3 PASS**.
- CTest hardware: `northstarcpp_device_probe_d3d12_postfx_hardware` PASS.
- Git diff --check on tracked changes PASS.

1000 GPU-frame probe (Vulkan Deferred, contact shadows enabled):
| Settings | p50 ms | p95 ms | p99 ms |
| --- | ---: | ---: | ---: |
| SSAO disabled | 0.0940 | 0.1256 | 0.1291 |
| SSAO enabled | 0.1039 | 0.1203 | 0.1308 |

These are synthetic GPU frames, not a matched Night City A/B benchmark.
They do **not** justify claiming a percentage FPS improvement.

## Next engineering steps (unchanged from backlog)
P0: actual directional CSM shadow array and caster passes in Vulkan,
including alpha-masked/skinned casters, cascade stabilization and PCF.
P0: HDR FP16 scene-color target, true post-forward compositor, bloom
pyramid, temporal exposure and filmic tonemapping shared across providers.
P1: temporal velocity/TAA and AO history, shadow caching, volumetrics,
SSR and HZB-based GPU culling with measured validation-layer coverage.

No Git commit/push and no production `dist` replacement. Other current
working-tree edits have been preserved.
