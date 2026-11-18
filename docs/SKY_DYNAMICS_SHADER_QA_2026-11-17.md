# NorthStarCpp — Engine Sky movement, scattering and shader QA

Date: 2026-11-17. Workspace: `NorthStarCpp`.
> **Историческая приёмка до GPU Sky.** Эта страница фиксирует первоначальные шесть проверок. Позднее добавлены покадровые GPU Sky uniforms, Vulkan async Capture API и ещё три специализированных теста. Актуальные результаты 11/11 проверок и ограничения GPU-шейдеров описаны в `docs/GPU_SKY_ATMOSPHERE_AND_VULKAN_CAPTURE_2026-11-17.md`.

Status: **6/6 targeted Sky CTest checks PASS**. Full engine suite is not claimed green.

## Method and test coverage

1. **Native solar and cloud simulation.** `tests/sky_dynamics_scattering_smoke.cpp` drives the production `SkyEnvironment` at the equator during the spring equinox. Check 06:00 (solar altitude 0°), 12:00 (+90°), and 18:00 (0°), the rendered twilight and sun disk, haze intensity, low/high cloud coverage, cloud advection, real source flow-map interface, and pause/freeze determinism. The test uses real `render_rgba()`, not a rewritten approximation.
2. **Real GPU dynamic sky.** `tests/sky_gpu_dynamics_smoke.cpp` dynamically loads `render_d3d12.dll` or `render_vulkan.dll`, submits the exact `NorthStarSky` mesh + composited texture through the production render ABI, draws to a Win32 swapchain and tests post-update frames. D3D12 additionally requests and reads back the true RGBA GPU framebuffer through `NorthStarRenderCaptureApi`; WARP and hardware are tested separately. Vulkan executes `verify_pixels=1` internal GPU readback, but does not expose the capture extension needed for an actual per-pixel dynamic-parity assertion.
3. **GPU shader compilation.** `tests/sky_hlsl_compile_smoke.py` compiles the actual sky shader literals from the D3D11 and D3D12 shader compilers with Windows D3DCompile to DXBC, VS/PS shader model 5.0. Vulkan shares the generic textured, unlit material path; there is no dedicated Vulkan sky scattering shader yet.
4. **Real asset origin and loading.** `tests/sky_assets_smoke.py` validates 22 original sky PAKs, 198 VRAM descriptors, 101 preserved original DDS files (including BC6H HDR), 57 original sky and 42 cloud YTD entries, 99 PNG previews, and 22 native preset references. Runtime logs confirm the actual `sky-act2-watchtower-overcast` preset loads four layers from `sky.ytd/clouds.ytd` on both D3D12 and Vulkan. The GPU frame-delta QA uses deterministic synthetic cloud maps for repeatability; it does not yet produce full-preset screenshot parity.

## Actual results

Six registered `northstarcpp_sky_*` CTests: **6 passed, 0 failed**, wall time ~1.61 seconds in one observed run.

Solar position at equinox/equator: sunrise 0°, noon +90°, sunset 0°.

Changed CPU pixels (256×128 pixels, threshold RGB absolute sum >3):

| Transition | Changed pixels |
|---|---:|
| Sunrise → noon | 32,768 |
| Noon → sunset | 32,768 |
| Haze 0 → 1 | 15,360 |
| Cloud coverage 0 → 0.95 | 16,374 |
| Cloud flow after 6 s | 10,783 |
| Authored layered sky vs default | 32,735 |
| Authored flow maps after 4 s | 10,511 |

Actual **D3D12 RGBA GPU readback** at 384×261 (changed pixel threshold >9):

| Transition | Hardware | WARP |
|---|---:|---:|
| Noon → solar sunset at equinox | 99,084 | 99,041 |
| Cloud coverage 0.30 → 0.96 | 47,464 | 47,345 |
| Animated cloud flow after 5 s | 23,973 | 23,895 |

D3D11 and D3D12 `sky_vertex_main / vs_5_0` and `sky_pixel_main / ps_5_0` compile successfully using the real HLSL source.

Both Vulkan and D3D12 Player launches continued running beyond 8 seconds with the real `sky-act2-watchtower-overcast` 4-layer source loaded. Earlier missing `sky_sources.xml` warning was not reproduced; `sky_sources.xml`, `sky.ytd` and `clouds.ytd` are present in the current `northstar_shared.nsp`.

## Critical uncovered limitations

**GPU atmospheric scattering is not implemented as a physical sky shader.** Sunrise/sunset coloration, haze, sun disk and cloud projections are computed by the C++ CPU sky texture compositor. D3D12/D3D11 sky pixel shaders sample one RGBA8 texture; Vulkan renders sky as an ordinary unlit material. Rayleigh/Mie physical scattering LUTs, dynamic shader uniforms, cloud volumetrics, cloud shadows, and temporal reprojection are not yet implemented.

**CPU composition hitches:** 16 measured 512×256 RGBA renders with authored layers: ~48.6–52.2 ms observed range across standalone measurements, p95 48.9–52.0 ms. This excludes GPU `set_sky()` upload, fences and PSO. Native gameplay currently refreshes the sky texture approximately every 5 simulated seconds, which is a coarse visible animation cadence and can stall a main thread. The values do **not** represent game FPS or frame time p95.

**Vulkan parity requires an additional capture interface.** Its Sky test does actual `set_sky`, render, `verify_pixels=1` for four time/cloud states; however pixel-level equivalence with D3D12 is **not** verified because the Vulkan provider lacks `NorthStarRenderCaptureApi`.

## Repeatable commands

```powershell
cmake --build out/build --target northstarcpp_sky_dynamics_scattering_smoke northstarcpp_sky_gpu_dynamics_smoke -j 4
ctest --test-dir out/build --output-on-failure -V -j 2 -R "^northstarcpp_sky_"
python tests/sky_assets_smoke.py
```

Future acceptance criteria: asynchronous GPU sky LUT updates or GPU-computed atmosphere with continuous per-frame sun/cloud uniforms; Vulkan capture and GPU screenshot diff (day/dusk/night, clear/overcast); shader compile/reflection validation on both backends; frame-time p95/p99 and hitch count measured in exactly the same actual scene.

No `dist` deployment and no commit/push were performed as part of this QA task.
