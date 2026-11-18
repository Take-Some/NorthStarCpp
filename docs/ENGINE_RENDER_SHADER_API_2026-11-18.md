# NorthStarCpp — root Engine Render shader API

**Date:** 2026-11-18
**Implementation:** Native V8 bindings, common SDK facade, TypeScript declarations, provider shader reload bridge

## Design contract

The canonical top-level engine API is `API.render`. It is separate from the compatibility `API.rendering` facade; both continue to share the same native renderer. The global `API.sky` service configures the simulated sky, and its built-in shaders live in the same **SharedAssets VFS** that every project mounts.

The root shader catalog currently includes:

- `shaders/sky/atmosphere.glsl` — Vulkan sky atmosphere, sun HDR disc, cloud fields
- `shaders/sky/atmosphere.hlsl` — D3D12 sky atmosphere and sun
- `shaders/sky/sun_optics.glsl` — shared Vulkan lens/shaft shader source
- `shaders/sky/sun_optics.hlsl` — shared D3D12 lens/shaft shader source

The native providers embed these sources at build time using
`tools/dev/embed_engine_sky_shaders.py`; global VFS source copies live at
`engine/northstarcpp/resources/sharedAssets/shaders/sky`.
**Source availability is distinct from rendering activation.** The root
lens/shaft files being catalogued does not establish that every provider
has completed depth-occluded postprocessing integration.

## API usage

```javascript
import { API } from "@northstar/engine";

const catalog = API.render.shaders.catalog();
// {provider: "vulkan" | "d3d12" | null, hotReload: boolean, builtin: [...]}

const source = API.render.shaders.source("shaders/sky/atmosphere.glsl");
const file = API.render.shaders.load("shaders/sky/sun_optics.hlsl");
// {path,language,source}

// Sources authored inside the project are also available:
const water = API.render.shaders.load("shaders/water.frag");

// For a material referencing shaders/water.frag, where an external
// build task has produced assets/shaders/water.spv:
const affectedMaterials = API.render.shaders.queueCompiled(
    "shaders/water.frag","shaders/water.spv");

// Call in a later update tick, not as a blocking spin loop.
const {pending,completed} = API.render.shaders.poll();
```

`API.render.provider()` returns `null` during script entrypoint
initialization (before viewport/renderer initialization). The catalog
and source operations are **independent of GPU initialization**.

## Native bindings

`window_script_bindings_host.cpp` defines the `engine.render` binding
namespace with operations:

- `shaderCatalog()`: authoritative engine root paths, selected provider,
  active shader reload extension availability
- `shaderSource(vfsPath)`: read a bounded UTF-8 shader asset via
  `AssetStore`; no direct renderer/provider filesystem access
- `shaderQueueCompiled(sourceVfsPath,compiledVfsPath)`: read a prebuilt
  SPIR-V or DXIL asset from VFS; forward to the **existing**
  `Viewport::queue_fragment_shader_reload`, matching source shader
  paths against currently active YMAT material references
- `shaderPoll()`: query existing nonblocking GPU pipeline reload status

Vulkan/D3D12 use their respective existing Shader Reload ABI v1/v2.
No new GPU shader compilation path has been invented.

### Safety and lifetime constraints

- `shaders/`-prefixed paths only, ASCII characters, up to 256 bytes;
  reject traversals (`..`), backslashes, colons and malformed extensions
- Sources limited to 256 KiB; binary hot reload files limited by
  `NORTHSTARCPP_SHADER_RELOAD_MAX_BINARY_BYTES` (1 MiB)
- Reload requires an unpacked project, mounted project source path,
  existing material reference and provider shader reload support
- Symlinked project source path components are rejected
- A failed source read/compile/provider reload throws a script-level
  error; it does **not** silently install an invalid graphics pipeline
- Source data and compiled bytecode remain distinct: `queueCompiled`
  **does not compile source**. Project build tools compile HLSL/GLSL.

## Verification

- `node tests/render_shader_api_smoke.cjs` — PASS, including path and
  language validation, catalog immutability and reload delegation
- `node tests/sky_script_api_smoke.cjs` — PASS
- `node tests/abby_pistol_demo_smoke.cjs` — PASS
- `python tools/dev/compose_northstar_bootstrap.py --root engine/northstarcpp/resources/sharedAssets/api --verify` — PASS
- `python tools/dev/embed_engine_sky_shaders.py --output engine/northstarcpp/modules/render/shared/engine_sky_generated.hpp --verify` — PASS
- `python tests/sky_hlsl_compile_smoke.py` — PASS, VS/PS for D3D11 and D3D12; updated to read the canonical D3D12 sky source
- `window_script_bindings_host.cpp` compiled successfully standalone
  using the project's real Ninja C++ compilation command
- An isolated native Player was successfully linked as
  `out/build/northstarcpp_shader_api_smoke_player.exe`. Actual V8
  runtime-init smoke passed on **Vulkan** and **D3D12**, reading all
  four shared Sky shaders and one project-authored shader from VFS,
  polling the shader queue and rejecting path traversal

### Environment limitations

Normal `cmake --build out/build --target northstarcpp_player`
was blocked by a locked `northstar_shared.nsp` archive and an unrelated
current Jolt source error (undefined `advance_city_vehicles`).
The currently running Player EXE was also locked; it was **not**
overwritten. The isolated linked Player proves the new native bindings,
but is not a full clean production rebuild.

Existing `engine_api_layout_smoke.cjs` fails because an existing set of
28 `.bak` files remains inside the active SDK from workspace edits.
This file-cleanliness failure is independent of Shader API behavior.

No production `dist` changes, commit or push were performed.
