# NorthStarCpp — unified atmosphere, cloud anchoring and horizon fog
Date: 2026-11-18

## Scope and actual findings
The canonical Sky HLSL/GLSL already contained spherical-planet single-scattering
Rayleigh/Mie, sun-disc integration, 3D Perlin/Worley altitude-bounded cloud
density, animated 2D weather coverage, light marching (6 taps) and adaptive
view marching (16–28 samples). This change extends that implementation. It
does **not** claim to introduce full multiple-scattering LUTs or temporal
reprojection.

The original Vulkan cloud shader incorrectly offset cloud density using
interpolated **sky mesh normals** rather than actual camera position. Also,
the existing aerial fog was opt-in through an environment variable and did not
affect untextured forward-rendered world cubes when deferred rendering was off.

## Implemented
- Provider-neutral project/native/JS `engine.sky.fogDensity`, validated in
  `[0, 0.25]`, default 0 to keep other projects unchanged.
- Provider Sky UBO extends the old five-vec4 payload with an optional camera
  world-space vec4 tail. Vulkan shaders receive real camera XYZ each frame;
  D3D12 preserves its existing original five-vec4 + camera position + view
  constant-buffer layout.
- Vulkan 3D cloud noise/world weather is offset by the real camera, not
  interpolated skydome normals; D3D12 already used camera-world coordinates.
- Both sky shaders blend Earth-like ground scattering and the lower hemisphere
  through a smooth horizon aerosol veil. The same daylight fog tint is used
  in the D3D12/Vulkan deferred passes.
- Vulkan forward **basic/world** geometry now receives conservative height fog
  extinction. The RGB fog tint is composed in GLSL based on per-vertex
  transmittance. The source shaders were recompiled to SPIR-V and only the
  required `basic_fragment` and `world_fragment` arrays were regenerated
  using `tools/dev/embed_vulkan_forward_fog_shaders.py`.
- The existing AbbyPistolDemo manifest retains its hybrid sky drawable but
  adds `cloudCoverage: 0.72`, `haze: 0.46`, `fogDensity: 0.045`.
  The demo generator uses these defaults for newly generated projects.
- Added sky environment validation and project manifest regression assertions.

## Build and tests
- SDK `compose_northstar_bootstrap.py`, JS sky smoke: PASS.
- HLSL D3D11 and D3D12 VS/PS shader compilation: PASS.
- Canonical sky embed `embed_engine_sky_shaders.py`: PASS.
- `northstarcpp_sky_environment_smoke.exe`: PASS; validates authored fog.
- `northstarcpp_sky_dynamics_scattering_smoke.exe`: PASS.
- Compiled `sky_project_contract_smoke.cpp` separately into
  `out/build/sky_project_contract_fog_qa.exe`: PASS.
- `sky_assets_smoke.py`: PASS, authentic YTD source assets retained.
- `create_abby_pistol_demo.py --verify`: PASS.
- Both `render_d3d12.dll` and `render_vulkan.dll` rebuilt successfully.
- GPU sky smoke with `--gpu-uniforms --view-sweep` passed on Vulkan and
  D3D12/WARP: 40 tested view directions, full 100224/100224 colored pixels,
  day/night/coverage/wind changes. World camera translation of 4.5 km changed
  the cloud field (Vulkan 39,334 pixels; D3D12 30,296).
- `northstarcpp_render_vulkan_smoke.exe render_vulkan.dll`: PASS actual Vulkan
  geometry/resize/readback after compiling forward fog SPIR-V.
- `git diff --check`: PASS (line-ending warnings only).

## Important limitations / acceptance blockers
- The new fog shader on Vulkan forward geometry interpolates transmittance
  per vertex. For very large triangles, analytic per-fragment atmospheric
  integration would be more accurate.
- True temporal reprojection/history accumulation and disocclusion rejection
  are **NOT IMPLEMENTED** here; current stable per-pixel spatial jitter is
  not a substitute for real cloud history.
- Existing atmospheric scattering is single-scattering, not full
  multi-scattering LUT / physically complete Earth atmosphere.
- Full `cmake --build out/build --target northstarcpp_player -j4`
  succeeded after earlier lock/timeouts: both render providers and the
  updated Player executable linked with exit code 0. Nonfatal Boost RTTI
  duplicate-section size warnings remain for a separate clean-build review.
- End-to-end `tests/weapon_live_ads_alignment.py` on the newly built Player
  passed with both **Vulkan** and **D3D12/WARP**: Player exit 0, ADS socket
  alignment retained, muzzle unchanged. These are integration results,
  NOT visually approved sky screenshots or measured GPU parity/performance.
- No production `dist` replacement; changes remain local.
- No cross-backend image-difference/pixel-level parity measurements, no
  frame-time/1%-low performance claims, no camera-in-motion history tests.
- Keep the existing Player/Editor closed to unload old renderer DLLs before
  testing a future clean launch.
