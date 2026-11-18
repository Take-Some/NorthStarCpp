# NorthStarCPP — Engine Sky API (initial implementation)

Date: 2026-11-17

> **Дополнение 2026-11-17:** покадровые GPU Sky uniforms и Vulkan Capture ABI уже реализованы. Приведённое ниже описание пятиисекундного CPU-обновления относится к fallback-провайдерам. Подробности: `docs/GPU_SKY_ATMOSPHERE_AND_VULKAN_CAPTURE_2026-11-17.md`.


## Goal

`engine.sky` is the single public script API for world atmosphere. It is independent of a particular game scene, and the same authored NorthStarSky/YDD/YMAT/YTD sky geometry and textures are presented through D3D12 and Vulkan. Other game script systems do not own duplicate clocks.

## Canonical root entrypoint

`API.sky` is now the primary user-facing API, on the same root level as `API.physics` and `API.audio`. The canonical source is `resources/sharedAssets/api/sky/sky.js`; `rendering/sky.js` is retired and no longer loaded. The named import `Sky` is preserved as an alias to the same class.

```javascript
import { API } from "@northstar/engine";
API.sky.setTimeOfDay(18, 30);
API.sky.setClouds({coverage:0.55, speed:0.02});
API.sky.setSun({intensity:1.2});
API.sky.setMoon({phase:0.5});
API.sky.setStars({density:0.7});
```

The native root `engine.sky.configure({...})` now accepts a structured JS object directly, in addition to the legacy JSON-string form. The native object bridge was verified by a headless V8 Player smoke test (`out/qa/sky_root_api_native_fixture`). Both Vulkan and D3D12 launched AbbyPistolDemo with `API.sky.configure` active.

## Public JavaScript / TypeScript

```javascript
import { Sky } from "@northstar/engine";

Sky.configure({
    enabled: true,
    timeOfDay: 17.5,        // hours 0..<24
    dayLengthSeconds: 1800, // real seconds for one in-game day
    latitudeDegrees: 42,
    dayOfYear: 172,
    moonPhase: 0.5,         // 0=new, 0.5=full, 1=new
    clouds: true,
    cloudCoverage: 0.42,    // 0..1
    cloudSpeed: 0.025,
    haze: 0.2,
    sun: true, moon: true, stars: true,
    sunIntensity: 1,
    moonIntensity: 0.25,
    starDensity: 0.55
});

Sky.setTimeOfDay(21, 30); // 21:30
const state = Sky.state();
console.log(state.sunDirection, state.moonDirection);
console.log(state.sunElevationDegrees, state.daylight);
console.log(state.moonIllumination, state.cloudOffset);

Sky.pause(true);
Sky.pause(false);
Sky.setCloudCoverage(0.8);
// Sky.advance(dt) is for deterministic test/cutscene stepping;
// the engine frame loop advances the clock during normal gameplay.
```

A new game that does not opt in to Sky preserves its existing authored texture/sky behavior, because the state defaults to disabled. `Sky.reset()` returns to that default. The runtime validates keys and numeric ranges both in the JS SDK and in native bindings.

## Source layout

- `modules/api/include/northstarcpp/sky_environment.hpp`: header-only, renderer-independent `SkyEnvironment`, `SkySettings`, `SkySnapshot`. Solar position uses game time, latitude and approximate seasonal declination. Lunar position and illumination are simplified cyclic models. The CPU reference composes a sky RGBA texture including sun and moon discs, nights, stars, moving cloud mask, twilight, and haze.
- `resources/sharedAssets/api/sky/sky.js`: **canonical root** `API.sky` facade, with `EngineCapabilityError` on missing native binding.
- `resources/sharedAssets/api/core/sdk.js`: `NorthStar.modules.engine.Sky` SDK export and `API.sky` alias, accessible via named `@northstar/engine` import.
- `resources/sharedAssets/api/manifest.json`: load ordering.
- `resources/sharedAssets/api/types/northstar-engine.d.ts`: public `SkySettings` / `SkyState` types.
- `modules/editor/window_script_bindings_host.cpp`: native `engine.sky.configure/state/setTimeOfDay/advance/reset` methods; rejects malformed and unknown fields, logs initial config.
- `modules/editor/window_runtime_frame_loop.inc`: simulation clock respects pause; the sky texture is refreshed at most once per 5 simulated seconds to bound synchronous GPU upload costs in the current sky renderer ABI.
- `modules/viewport/viewport.cpp`: authored sky asset reuse and GPU-provider-neutral procedural UV sphere fallback when no YDD sky exists. Both use the established `NorthStarSky` native renderer API.
- `modules/viewport/viewport_draw.cpp`: native directional sun/moon lights and sky ambient light, using the shared `render_lights_api.h` sentinels.

## Tests and integration

- `tests/sky_environment_smoke.cpp`: noon/midnight at equator/equinox, solar elevation, full-moon direction, day/night image difference, clock pause, input validation, reset; CTest `northstarcpp_sky_environment` PASS.
- `tests/sky_script_api_smoke.cjs`: native bridge contract and JS API registration, configure/time/manual step/pause/cloud validation; PASS.
- `tests/abby_pistol_demo_smoke.cjs`: game-script bootstrap accepts configured Sky alongside existing player and weapon; PASS.
- Player plus Vulkan and D3D12 sky routes built/linked on Windows. Both launched AbbyPistolDemo with the actual Sky binding active and were still running after 7 seconds, no scripting/render exceptions in collected startup logs.
- Local QA logs: `out/qa/engine_sky_final_vulkan_20261117.log` and `out/qa/engine_sky_final_d3d12_20261117.log`.
- No production `dist` replacement, commit or push.

## Accuracy and performance boundaries

This is a functional first implementation, **not yet** a physically-based atmosphere or weather simulation:

1. Sun is a simplified local-solar model using latitude and season; the Moon uses approximate phase/position, not a precise astronomical ephemeris.
2. Clouds are a layered texture mask with drift; they are **not volumetric raymarched clouds**. No cloud shadow maps or precipitation yet.
3. The dynamic sun disc and sky texture are refreshed on a coarse 5-second cadence because current Vulkan/D3D12 `set_sky` APIs upload textures synchronously. Continuous 60-FPS celestial motion requires a dedicated GPU sky/environment uniform ABI, shader LUTs and asynchronous update, rather than per-frame 512² CPU texture uploads.
4. The existing YDD/YTD dome uses retained geometry/texture; enabling `Sky` composites the sky atmosphere into its rendered texture, but **full original artistic shader preservation** is a future sky material graph integration.
5. Frame-by-frame GPU capture comparison, quantitative frame-time/1% lows and lighting parity under day, sunrise, sunset, moonlight remain future acceptance tests.
6. The current native `engine.sky` implementation is built-in; the public API is independent of render backend, but a hot-swappable external **sky provider ABI** is not yet exposed.

## Next steps

Introduce `NorthStarSkyEnvironmentApi` (GPU sky uniforms and LUTs) for D3D12/Vulkan with double-buffered updates, atmospheric Rayleigh/Mie scattering, volumetric clouds and cloud shadows, synchronized sun/moon directional lighting and exposure, sky authored presets in XML/NEF8 assets, and a Sky Editor panel for timeline/season/weather authoring.
