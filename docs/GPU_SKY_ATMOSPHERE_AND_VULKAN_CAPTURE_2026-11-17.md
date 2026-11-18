# NorthStarCpp — GPU Sky Atmosphere, per-frame uniforms and Vulkan asynchronous capture

Date: 2026-11-17. Status: implementation and targeted QA complete, no production deployment.

## Objective

Move dynamic sky presentation from CPU RGBA texture regeneration to GPU shading while preserving the public `API.sky` / native `engine.sky` script API and both D3D12/Vulkan backends.

## Provider-neutral ABI

`engine/northstarcpp/modules/api/include/northstarcpp/render_sky_api.h`

- `NORTHSTARCPP_RENDER_SKY_MODULE_ABI = 0x534b5901`; independent of existing Render ABI 5.
- `NorthStarGpuSkyUniforms`: 5 × float4 = 80 bytes.
- Sun and moon direction, daylight/haze/scattering intensity, cloud coverage/UV speed/phase, lunar illumination, enable flags.
- `set_uniforms(renderer, source)`: validates finite/range data and copies the state into renderer-local storage; **does not create textures, issue GPU submissions or block on GPU**.
- Providers lacking the optional extension remain on the legacy CPU-composited sky fallback.

### Host

The viewport discovers `NorthStarRenderSkyApi` as an optional module extension and sends it on each simulation frame. The native `SkyEnvironment` clock remains authoritative. `Viewport::set_sky_environment()` submits a sky texture and geometry **once** when the source/preset changes, then returns early after uniform upload on subsequent frames. Frame loop `window_runtime_frame_loop.inc` no longer calls CPU `render_rgba` on every five-second sky transition when GPU Sky is active.

Original authored `sky.ytd` / `clouds.ytd` sources are still discoverable; the existing sky geometry/YDD and base YTD texture remain in use. The current GPU shader samples a retained sky texture for the two moving cloud projections; it does not yet bind all four original flow/projection textures independently in Vulkan/D3D12.

### D3D12

- `renderer_private.hpp` owns a copy of `NorthStarGpuSkyUniforms`.
- `renderer_scene.cpp` validates and stores updates.
- `renderer_frame.cpp` writes the 80-byte state to the existing mapped **256-byte sky constant buffer (CBV b1)** only **after waiting on the previous frame fence**. This avoids a race with an in-flight graphics submission.
- `shader_compiler.cpp` computes sky color, approximate Rayleigh and anisotropic Mie **single-scattering phase functions**, sun disc, horizon haze, moon, cloud UV flow and varying cloud luminance in the sky pixel shader. The legacy sampling path still applies when the optional GPU Sky flags are disabled.

### Vulkan

- `renderer_materials.cpp`: allocates a host-visible, coherent 80-byte sky UBO and adds uniform binding 6 to the existing material descriptor sets. Existing regular/material graph fragment shaders ignore this unused binding.
- `renderer_frame.cpp`: after waiting on the previously submitted frame fence, writes the 80-byte state to the UBO; never reuploads a texture on uniform-only changes.
- `material_spirv.hpp` and `renderer_materials.cpp`: enable the `NS_GPU_SKY` shader variant for the sky material only. It computes the same single-scattering approximation, cloud flow and luminance changes in the fragment shader. The sky is a regular unlit background material and is registered on `set_sky()` even when the scene does not call `set_material_batches()`.
- `vulkan.cpp`: exports `NorthStarRenderSkyApi`.

## Real Vulkan async GPU capture

`NORTHSTARCPP_RENDER_CAPTURE_MODULE_ABI` is now exported by Vulkan. It implements the same public capture extension used by D3D12.

- `capture.request()` marks the next frame for a `vkCmdCopyImageToBuffer` to a host-visible, coherent staging buffer. No queue idle is issued.
- A per-frame fence marks completion. `capture.read()` polls `vkGetFenceStatus`; it returns not-ready while GPU work is in flight. It does **not** block the CPU waiting for the GPU.
- On completion, data is copied into a stable, renderer-owned RGBA8 vector, converting from BGRA8 when appropriate. The next draw preserves pending capture pixels before reusing its staging buffer.
- Single capture request in-flight; request while pending fails rather than overwriting an in-flight image. The existing synchronous `frame.verify_pixels` mode remains available for targeted legacy assertions.
- This is an **asynchronous staging readback with polling**, not a multiframe ring-buffered capture queue. Capture throughput optimization remains a future task.

## QA / measured results

Commands:

```powershell
cmake --build out/build --target northstarcpp_player northstarcpp_sky_gpu_dynamics_smoke -j 4
ctest --test-dir out/build --output-on-failure -j 3 -R '^northstarcpp_(sky_|vulkan_hardware$|d3d12_hardware$)'
python tests/sky_assets_smoke.py
```

**11/11 relevant CTests passed:** nine Sky tests including CPU dynamics, original CPU/GPU sky replay, D3D12 Hardware/WARP and Vulkan GPU-uniform-only variants, shader compilation; plus the two general GPU hardware smokes. The Sky GPU-uniform-only variants exercise **60 consecutive simulation frames** updating the sky without resetting the texture. The test asserts only **one `set_sky` call** across changing conditions, then verifies distinct framebuffer pixels for sun, cloud density and flow.

In one single-texture-four-state GPU uniform-only run, actual changed framebuffer pixels (384×261, threshold RGB channel sum >9):

| Transition | D3D12 Hardware | Vulkan |
|---|---:|---:|
| Noon → sunset | 99,087 | 100,224 |
| Cloud density change | 38,929 | 47,677 |
| Cloud flow phase +5 seconds | 2,649 | 16,788 |

GPU readback is real on both backends. **Do not use the absolute pixel delta numbers to claim pixel-perfect D3D12/Vulkan parity:** colorspace and shader implementations are not yet matched numerically. D3D12 WARP passed the same test.

AbbyPistolDemo Player successfully launched with the active `API.sky` script and four original sky texture layers on both Vulkan and D3D12; it remained running after nine seconds and did not report sky/renderer errors. No actual game-specific p95/p99 frame-time or 1% low FPS benchmark is claimed here. A previous CPU-only 512×256 compositing benchmark averaged approximately 49–52 ms/update; eliminating repeated CPU work removes that particular update from the GPU-enabled frame path, **but does not prove a specific FPS improvement**.

## Remaining architectural work

1. Implement physically grounded Rayleigh/Mie optical-depth LUTs (transmittance, multiple-scattering, sky-view and aerial perspective), light/shadow integration and HDR exposure; present phase functions are intentionally approximate and are not a multi-scattering solution.
2. Bind the original four `sky-layered.fx` cloud maps separately to GPU: cloud projection, breakup, flow-phase and atmospheric HDR, instead of reusing a single retained sky texture for moving UV samples.
3. Add volumetric clouds, sun occlusion, cloud shadow maps and temporal reprojection.
4. Establish quantitative GPU shader/per-frame performance p95/p99, GPU upload counts and hitch count in the actual KaylaNightCity/AbbyPistolDemo scene, under equivalent D3D12 and Vulkan conditions.
5. Consider multiframe capture ring slots if frequent automated GPU screenshots become a bottleneck.

No `dist` replacement; no commit or push.
