# Engine Sky: project-owned source, Player/Editor parity (2026-11-18)

## Problem

The Win32 Player bootstrap hardcoded both `models/skydome.ydd` and
`models/skydome.ydd@skydome_high`. Qt Editor only mounted the project VFS,
so no `set_sky()` or procedural atmosphere was ever submitted by its GPU
Viewport. Vulkan lacked an analytic editor grid provider extension, causing
the fallback geometry strips to extend across the background.

## Implemented

- Added `sky_project_config.hpp`: safe, validated, provider-neutral
  `sky` metadata from `northstarcpp.project.json`. When no manifest or
  sky section is present, a procedural atmosphere is enabled by default.
- Supported modes: `disabled`, `procedural`, `drawable`, `hybrid`.
  Optional `drawable` is a VFS YDD@element or YDR reference, not a C++
  special-case filename. Unsafe absolute, parent-traversal and non-model
  references are rejected before resource resolution.
- Win32 App loads authored settings **before** project scripts. Project
  scripts can then override the clock, sun, clouds, mode and drawable using
  `engine.sky.configure()`. The existing `SkyEnvironment` remains the
  single clock for all render providers.
- `Viewport::set_sky_asset_reference()` uses `AssetStore` for YDD/associated
  YMAT/YTD. Resource errors fall back to procedural atmosphere when
  possible without aborting the Player. Previous project sky is invalidated
  when a different VFS is mounted.
- `Viewport::configure_project_sky()` is used by Qt Editor without executing
  game JavaScript. Both Editor and Player submit through the common
  `NorthStarSky` and `NorthStarGpuSkyUniforms` provider contract.
- Removed both original skydome string literals from
  `window_app_internal.hpp`. The Abby demo's source path is explicitly
  authored in `Projects/AbbyPistolDemo/northstarcpp.project.json`.
- Vulkan GPU atmosphere uses a fullscreen sky pass. Authored, non-GPU sky
  meshes now preserve original YDD triangles/UVs and clip triangles against
  the near plane instead of dropping any triangle with one near-clipped
  vertex; both sky modes render at far depth behind opaque geometry.
- Vulkan's legacy editor-grid geometry is bounded within a 30..190 m
  camera-relative disk and fades near its edge, instead of rendering
  near-infinite cube strips across the horizon. D3D11/D3D12 native analytic
  grid APIs were not changed. This is a bounded fallback, **not yet full
  Vulkan analytic-grid parity**.

## Example project manifest

```json
{
  "sky": {
    "mode": "hybrid",
    "drawable": "worlds/sky_assets.ydd@evening",
    "sun": true,
    "clouds": true,
    "timeOfDay": 17.5,
    "cloudCoverage": 0.4
  }
}
```

Without `drawable`, `mode: "procedural"` uses no sky model.

## Script example

```js
engine.sky.configure({
  mode: "procedural",
  sun: true,
  clouds: true,
  timeOfDay: 11.0
});
```

To select an authored model, use
`engine.sky.configure({mode:"hybrid", drawable:"worlds/sky_assets.ydd@evening"})`.
The C++ renderer never interprets this virtual filename.

## Verified

- `northstarcpp_viewport`: PASS (post-Vulkan grid update).
- `northstarcpp_render_vulkan`: PASS, DLL linked (post native sky UV/clip fix).
- `northstarcpp_player`: PASS, full game EXE linked (before subsequent grid-only change).
- `northstarcpp_editor_qt`: PASS, full Qt editor EXE linked before the
  subsequent grid-only change. Latest full relink attempts stop on
  `render_d3d12.dll: Permission denied` because the DLL is currently locked.
  No running application was terminated; the latest Editor EXE has **not**
  been rebuilt after the grid update.
- `tests/sky_project_contract_smoke.cpp`: PASS (native standalone C++20,
  all modes, invalid paths, Abby manifest).
- `node tests/sky_script_api_smoke.cjs`: PASS.
- `northstarcpp_sky_environment_smoke.exe`: PASS.
- `northstarcpp_editor_grid_smoke.exe`: PASS.
- GPU `northstarcpp_sky_gpu_dynamics_smoke.exe render_vulkan.dll`: PASS,
  real readback with sky illumination, clouds and time.
- GPU Vulkan `--gpu-uniforms`: PASS, single texture upload and 4
  uniform updates (also after the authored mesh near-plane fix).
- GPU D3D12 `--gpu-uniforms`: PASS.
- `git diff --check`: PASS on modified tracked sky sources.

## Open acceptance

A live Qt Editor screenshot/readback for AbbyPistolDemo with the latest
relinked Editor and Vulkan, plus qualitative grid AA/fade and source YDD
UV validation, remains pending. The Vulkan grid will ultimately need a
native analytic pass for D3D12-level parity. No production dist was
replaced, and no commit/push was performed. Backups are beside changed
sources using `.before-sky-contract-*`, `.before-sky-api-*`,
`.before-native-sky-clip-*`, and `.before-grid-*` suffixes.
