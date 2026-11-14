# NorthStarCpp — Module Loading

Each native engine capability is resolved by its `engine.*` ID, host ABI
and typed API table. An external provider is a `.dll` on Windows or a
`.so` on Linux. A builtin provider links into the engine and supplies the
same descriptor and table through an internal query function.

| Selection order | Source |
| --- | --- |
| 1 | Explicit `NORTHSTARCPP_<CAPABILITY>_PLUGIN` environment override |
| 2 | Explicit `plugins.overrides` in `bootstrap.json` |
| 3 | First compatible external provider in sorted path order |
| 4 | First compatible builtin provider in registration order |

The default search directory is the executable directory. The loader scans
one flat directory and validates the export, host ABI, requested API version,
table size and required callbacks. Automatic discovery skips incompatible files.
An explicitly selected file must implement the requested ABI. An active
context retains the provider that created it. Restart the application to apply
provider changes across all its contexts.

| Capability | Contract | Shipped provider |
| --- | --- | --- |
| `engine.ecs` | `ecs_api.h`, ABI 2 | Flecs (external) |
| `engine.physics` | `physics_plugin.h`, ABI 4 | Bullet or Jolt (external) |
| `engine.render` | `render_plugin.h`, ABI 5 | D3D11 (external) |
| `engine.audio` | `audio_plugin.h`, ABI 1 | WinMM (external) |
| `engine.input` | `input_plugin.h`, ABI 1 | Win32/XInput (external) |
| `engine.assets` | `assets_plugin.h`, ABI 1 and optional archive ABI 2 | Directory and PC FAT/DB VFS (external) |
| `engine.logger` | `logger_plugin.h`, ABI 1 | MultiSink (external) |
| `engine.perlin` | `perlin_plugin.h`, ABI 1 | Improved Perlin (builtin) |
| `engine.scripting` | `scripting_plugin.h`, ABI 2 | V8 (optional external provider) |

Use `northstarcpp_builtin_module` to link a provider's normal C ABI query
into a runtime and `northstarcpp_builtin_registry` to generate its registry.
External providers export `northstar_module_query` using
`NORTHSTARCPP_MODULE_EXPORT`. One library may supply multiple capabilities.
Optional extensions use the selected provider's table so that the same
provider owns every operation on its contexts. The loader and Perlin runtime
build on Linux. The editor, player and shipped Windows backends use the Windows
build.

## Script services

The public `script_service_plugin.h` contract lets a plugin implement a
whole `engine.*` script namespace or individual methods, including scene,
camera, animation, save, input, UI and session operations. Script services
use `northstar_module_query` with `NORTHSTARCPP_SCRIPT_SERVICE_ABI`. A library
may serve both the native ABI and the script ABI for the same namespace.

Explicit script selections use `NORTHSTARCPP_<CAPABILITY>_SCRIPT_PLUGIN` or
`plugins.scriptOverrides`. Capability environment names omit `engine.`, use
uppercase and replace any remaining dots with underscores.

A runtime owns one service context per used namespace. For supported methods
the runtime returns the provider's result or error. Unclaimed methods keep
their registered host implementation. A service may call that handler
synchronously through its supplied fallback callback, scoped to its own
namespace. Runtime reset clears service contexts and the next call resolves
them again.

## Logger API and output

The `engine.logger` provider emits UTF-8 text .log and structured JSON Lines
.ulog simultaneously, with UTC timestamps, severity, channel and thread IDs.
Use `<northstarcpp/logger.hpp>` from the SDK. The built-in MultiSink implementation
supports console/debugger sinks, thread-safe writes, log rotation and explicit
flush. Application startup/exit/FATAL events are recorded automatically without
making the logger a critical startup dependency.

Set `NORTHSTARCPP_LOG_DIRECTORY` to redirect application logs for testing, or
`NORTHSTARCPP_LOGGER_PLUGIN` to select a compatible provider DLL.

See [LOGGER.md](LOGGER.md) for code examples, sink descriptions and tests.

## AssetManager and VFS

Link `NorthStarCpp::northstarcpp_runtime` and include `<northstarcpp/assets.hpp>`.
`AssetManager` uses the existing `AssetStore` interface and the discovered
`engine.assets` provider. An ABI 1 provider can still serve directory mounts.

```cpp
northstarcpp::AssetManager assets;
assets.mount_archive("", "content.db", "content.db", 0);
assets.mount_directory("textures", "loose-textures", 10);
auto bytes = assets.read("textures/stone.bin");
```

Use forward slashes in UTF-8 virtual paths. The empty prefix mounts at the
virtual root. Higher priorities win, followed by the more specific prefix and
then the most recent mount. `size` and `read` work for both sources, and each
mount type has a matching unmount method.

The reader supports the original `fs_pack` PC layout with a combined file or a
separate FAT index and DB data file. It reads uncompressed external and inline
entries, validates index offsets, and verifies checksums when present. PPMd
entries report an unsupported compression error. Console formats and streaming
are pending. Buffered asset reads are limited to 256 MiB.

```powershell
.\northstarcpp_player.exe --archive .\content.db --scene levels/main.nscene
.\northstarcpp_player.exe --archive .\content.fat --archive-data .\content.db --scene levels/main.nscene
```

## Asset streaming

The host queues VFS reads and decoding on a background worker. Duplicate requests
for the same type and virtual path share a result. Critical requests move ahead
of queued normal and background work. Retain a request or its decoded handle
while a resource is in use. The cache evicts unpinned resources in least-recently
used order above the default 64 MiB decoded-payload budget. Live handles can
exceed that budget. Use `streaming_stats()` to inspect queued, loading and
resident resources. A full queue returns an empty request. Try again later.
Changes to the mount table invalidate cached resources and mark existing
requests stale. Existing decoded handles remain usable.

```cpp
auto sky = assets.request_drawable_resources("models/skydome.ydd@skydome_high");
if (sky.ready()) {
    auto resource = sky.get();
    // Use the decoded model and its material and texture dependencies.
}
```

The player can prefetch and retain drawables by camera or player position.
It reads `assets/streaming/zones.json` beside the project scene, or an explicit
manifest passed with `--asset-zones FILE`. `--shared-assets DIR` overrides the
shared content root. Positions are in world coordinates.

```json
{
  "version": 1,
  "zones": [
    {"min": [-25, -10, -25], "max": [25, 15, 25],
     "drawables": ["models/skydome.ydd@skydome_high"]}
  ]
}
```

A zone prefetches 20 units from its bounds and releases its request after a
further 5-unit exit margin. The render provider still needs a mesh/material
consumer to display the streamed drawables.

## Shared assets

Mount the engine's shared asset directory with `AssetManager::mount_shared_assets`.
It is mounted at the VFS root with priority -100. Project mounts with the default
priority can override shared resources at the same logical paths.

```cpp
northstarcpp::AssetManager assets;
assets.mount_shared_assets("sharedAssets");
auto sky = assets.load_drawable_resources("models/skydome.ydd@skydome_high");
```

`load_material`, `load_drawable` and `load_texture` select individual entries.
`load_drawable_resources` resolves the materials and their textures, preserving
each reference and its typed value. Textures retain native mip payloads.

Material libraries exclusively use `.ymat` (NEF8 kind 4 and native XML schema).
The historical `.ymt` material libraries have been converted. Skydome YDD
now refers to `materials/skydome.ymat@sky_cloud_clear`. Legacy
`.ymt` material payloads require conversion before importing.
`.ymt` is reserved for historical non-material/skeleton data; decoding
such data remains pending. The skydome YTD still validates its exact known
legacy body digest.

Use `northstarcpp_player.exe --check-shared-assets PATH` for inventory and
dependency diagnostics. The command reads YSC and GLSL source files as UTF-8.
The native player automatically discovers `scripts/main.js` in scene-file
projects when a compatible V8 provider is available. Scene V3 script components
also load through `engine.assets`, with lifecycle events and explicit ECS,
physics, scene and logger bindings. Archive script mounting passes V8/VFS and actual player GUI tests in
both source and archive modes; full installed/signed production-package
acceptance remains pending. Shader compilation and GPU
material binding are separate stages.

## bootstrap.json: changing the common directory

Place an optional `bootstrap.json` **next to the executable**. Without it, the loader scans `./` relative to the EXE. To redirect **all** capabilities:

```json
{
  "plugins": {
    "path": "./plugins",
    "overrides": {
      "engine.perlin": "./plugins/custom_perlin.dll"
    },
    "scriptOverrides": {
      "engine.scene": "./plugins/custom_scene.dll"
    }
  }
}
```

`plugins.path` and file overrides in this configuration resolve relative to
the executable and also accept absolute paths. Environment overrides resolve
relative paths against the current working directory. Invalid configuration or
missing configured paths produce explicit errors.

Release distributions include an editable `bin/bootstrap.json` sourced
from `config/bootstrap.example.json`. Development builds can copy the
example beside the executable. Removing the file restores built-in defaults.
Alongside `plugins.path`, the same file supports `logging.enabled`,
`logging.level`, `logging.directory`, sink settings, rotation and
`logging.console.visible`. The console is hidden by default, while
.log and .ulog journal files remain enabled.

See [LOGGER.md](LOGGER.md) for the complete configuration schema.

## Selecting an alternative physics provider

Both physics DLLs can coexist in one directory. To select Jolt without deleting Bullet, use the native Project Manager or an environment variable:

```powershell
$env:NORTHSTARCPP_PHYSICS_PROVIDER = "jolt"
.\northstarcpp_player.exe --scene .\scene.nscene
Remove-Item Env:NORTHSTARCPP_PHYSICS_PROVIDER
```

Provider switching takes effect when a new physics world is created. Active worlds retain their original provider. A configured or environment file override takes precedence over the physics provider name.

## Compatibility and security

The existing environment variables continue to work. An explicit ECS path also accepts the legacy northstar_ecs_get_api entry point. Render and physics retain their older extension exports. External providers run as native process code. Keep bootstrap.json, plugin directories and override binaries under trusted control.

[Scripting API](SCRIPTING_API.md) · [Perlin API](PERLIN_API.md) · [More about Jolt](JOLT_PHYSICS.md) · [Source layout](ENGINE_LAYOUT.md) · [Release builds](DISTRIBUTION.md).
