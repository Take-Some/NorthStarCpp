# Native V8 Provider — UCRT64 Build and Verification

Copyright (C) 2026 Kayla's Systems — Kayla Verner.

## Build architecture

NorthStarCpp's engine and C++ SDK use MinGW-w64 **MINGW64/MSVCRT** and C++17.
The official MSYS2 Windows V8 SDK is built for **UCRT64** and exports a
dynamic V8 C++ library. **Do not link its C++ SDK into the MSVCRT engine.**
Instead build `engine.scripting` as an independent UCRT64 DLL and communicate
only through `NorthStarScriptingApi` ABI 2 (C structs and borrowed UTF-8 data).
The provider does not expose V8 C++ handles or transfer STL objects across
the runtime boundary.

Source: `engine/northstarcpp/modules/scripting/v8/v8.cpp`
Separate CMake project: `tools/v8_provider/CMakeLists.txt`
Provider: `scripting_v8.dll`, capability `engine.scripting`, API version 2.

## Required SDK

A matching 64-bit MSYS2 UCRT64 GCC, V8 headers, V8 import libraries and
runtime dependencies. The MSYS2 package names are:

- `mingw-w64-ucrt-x86_64-gcc`
- `mingw-w64-ucrt-x86_64-v8`

Install through the official MSYS2 UCRT64 package manager, after addressing
any local pacman mirror, filesystem-permission or database-lock issues.
Do not remove a live pacman database lock or mix vendor/system GCC runtimes.

For offline development only, a *locally staged test SDK* was extracted from
existing signed package-cache archives into
`out/v8-sdk-cache-compat/ucrt64/`. This location is a build-tree cache, **not
the main development toolchain or a production dependency**.

## Reproduce the provider build

From the repository root in PowerShell (replace the SDK path with an installed,
updated UCRT64 SDK when available):

```powershell
$root = (Get-Location).Path
$v8 = "$root/out/v8-sdk-cache-compat/ucrt64"
$build = "$root/out/scripting-v8-ucrt-build"

cmake -S tools/v8_provider -B $build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER="$v8/bin/g++.exe" `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe `
  -DNORTHSTARCPP_ENGINE_ROOT="$root" `
  -DNORTHSTARCPP_V8_ROOT="$v8" `
  -DNORTHSTARCPP_HOST_SMOKE_EXE="$root/out/scripting-api-20261114/northstarcpp_scripting_smoke.exe" `
  -DNORTHSTARCPP_HOST_ASSETS_DLL="$root/out/scripting-api-20261114/AssetManager.dll"

cmake --build $build --parallel 4
ctest --test-dir $build -R northstarcpp_scripting_real_v8 --output-on-failure
```

The separate CMake project verifies the compiler's `_UCRT` macro. It
defaults to `V8_COMPRESS_POINTERS` and `V8_ENABLE_SANDBOX`, which match
the tested MSYS2 V8 build. Other V8 builds require their actual GN build
flags; do not guess. Build configuration mismatches can abort the process.

The build stages V8 and UCRT dependent runtime DLLs alongside
`bin/scripting_v8.dll`. The engine's provider search path can then use:

```powershell
$env:NORTHSTARCPP_SCRIPTING_PLUGIN = "$build/bin/scripting_v8.dll"
```

For a standalone game/editor, all runtime DLLs must also be resolvable by
the Windows loader. The game's regular DLL search and distribution manifest
rules apply; copying only `scripting_v8.dll` will not work.

## Verified behavior — 14 November 2026

The offline SDK cache provided V8 14.0.210 and isolated UCRT64 GCC 15.2.0.
This exact development configuration was built and verified. Checks performed:

- C ABI load/query; correct provider and table negotiation
- V8 ICU initialization with a real process executable path
- V8 Isolate creation, execution of `1 + 2` returning JSON `3`, teardown
- C++ to V8 calls and registered `engine.*` native callback invocation
- Stateful JS variables, events, reset, exceptions, UTF-8, asset-VFS load
- `while(true) {}` safely interrupted by watchdog and reported as timeout
- `FreeLibrary` no longer blocks: initialized V8 DLL is pinned for process lifetime

The V8 default platform owns background worker threads. The provider is
**process-lifetime resident after first initialization**; changing to a new
V8 provider requires restarting the game/editor. It is not a hot-swappable
engine plugin after V8 platform initialization.

## Existing game integration

The player already detects project-root `scripts/main.js`, starts a
`ScriptRuntime`, registers explicit engine bridges and emits `start`,
`update` and `stop` events. V3 scene nodes may contain `script`
references to `scripts/*.js`; they receive `entity.start`,
`entity.update` and `entity.stop` with stable IDs. Project sources
support hot reload that preserves the previous working context if reload fails.

Current explicitly registered methods include:

- `engine.logger.debug/info/warn/error(message)`
- `engine.scene.objectCount()`, `setLayerVisible(name, visible)`
- `engine.physics.applyImpulse(index, x, y, z)`
- `engine.ecs.entities/query/has/getPosition/setPosition`
- `engine.input.isDown(key)` and `engine.perlin.sample2(x,y)`

The C++/V8 bridge exposes only registered methods. External scripts are
**not trusted**: V8 in-process execution, heap settings and timeouts are
not isolation from memory exhaustion or native defects. Use a process
sandbox and stricter permissions for hostile/untrusted code.

## Deployment constraints and pending verification

The `out/` V8 artifact is **an integration build, not a signed public
release**. Before distributing it, install an updated supported V8 SDK,
review the current V8/security advisories and redistribution licenses,
ensure the final game includes all dependent DLLs, and run a packaged
player integration smoke. The project packaging tests cover script
sources and manifest checks. The development `northstarcpp_player.exe`
was successfully linked and a real 40-object project passed both source
and archive V8 gameplay tests (start/update/stop, saves, clean exit).
A fully installed and signed production game package has not been tested.

The native player now includes `--archive` script lookup using `engine.assets`.
Real VFS/V8 smoke and actual `northstarcpp_player.exe --archive` startup
both pass against the integration QA copy of NorthStarFirstPersonPlatform.
The test V8 remains unsuitable for public release pending security patching.
The regular scene-file project layout is still the principal runtime path.
