# NorthStarCpp 0.4.0 — FPS QA acceptance (2026-11-14)

## Status

Internal FPS authoring QA candidate: **PASS**. General availability:
**BLOCKED by independent signed release/security acceptance**.

This QA process leaves production dist unchanged.

## Build and test results

- Clean Ninja/MinGW Release build: 764/764 steps PASS, parallelism 2, exit 0.
- Complete CTest: 112/112 PASS, zero failures, 35.85 seconds.
- Dedicated FPS acceptance: 14/14 PASS, including project metadata, NEF8
  startup scenes, scene validation and actual engine initialization.
- Physics Bullet/Jolt, Flecs ECS, input, audio, asset streaming, NEF8,
  scripting/V8, HUD, D3D11, D3D12 and Qt Editor/Player integration are covered.
- First-party builtin ABI and author DLL/SO override precedence preserved.

## Installed QA engine

    out/qa-fps-dist-20261114/NorthStarCpp-0.4.0-windows-x64-Release-QA

Runtime, SDK, Tools, Documentation and Licenses are installed.
Manifest SHA-256: 3497e8d3277224c46305bbbddb226cabc0edbd19ab0b45cc613f148c7a2dea2c
Manifest files: 4537
Registered plugins: 14
Manifest verification: PASS.
Installed Player and SDK consumer smoke: PASS.
Bundled development V8 sidecar source/archive smoke: PASS.

## Exported working game examples

    out/qa-fps-dist-20261114/games/
    NorthStar-FPS-Test-0.4.0-windows-x64-Release-bullet/
    NorthStar-First-Person-Platform-0.4.0-windows-x64-Release-bullet/

The FPS arena export passed its standalone smoke and verified 69 installed files;
the exploration export passed its standalone smoke and verified 70 installed
files. Both project verify.cmd scripts exited 0 against the QA engine Player.
The FPS has weapons, moving targets, reloading and hazard health effects.
Exploration includes walking, grounded jumps, movable crates and authored JS.

## Reproduction commands

    cmake -S . -B out/fps-release-qa-20261114 -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
    cmake --build out/fps-release-qa-20261114 --target all --parallel 2
    ctest --test-dir out/fps-release-qa-20261114 --parallel 2 --output-on-failure
    python tools/verify_fps_release.py --build-dir out/fps-release-qa-20261114
    python -m tools.build_dist --qa-only --from-build out/fps-release-qa-20261114 --output-dir out/qa-fps-dist-20261114 --jobs 2
    python -m tools.build_dist --verify out/qa-fps-dist-20261114/NorthStarCpp-0.4.0-windows-x64-Release-QA --output-dir out/qa-fps-dist-20261114
    Projects/NorthStar_FPS_Test/verify.cmd
    Projects/NorthStarFirstPersonPlatform/verify.cmd

The builder refuses stale build graphs and overwrite of existing candidates.
Use a fresh QA destination for repeated packaging.

## Mandatory external shipping gates, not bypassed

1. Independently signed clean Windows VM acceptance on the exact QA bytes.
2. Security-approved, signed native V8 provider with matching SDK and binary;
   the current sidecar development provider is not certified for production.
3. Rights/provenance and dependency license review for historical source code.
4. Linux SO acceptance, extended real-hardware FPS performance benchmarks
   and long-run GPU/device-removal stress for general availability.

## Continuing engine development

- Migrate compatible historical skeletal animation, model and UI code to
  first-class engine/northstarcpp/modules modules; do not treat the archival
  core/northstarcpp tree as fully integrated simply because it is present.
- Expand the Qt Editor with real GPU viewport parity, plugin-driven ECS
  component inspector, FPS authoring and a complete animation timeline.
- Maintain one versioned engine.* ABI selection path for Player and Editor;
  explicit author DLL/SO override, compatible automatic external discovery,
  then builtin. Strengthen diagnostics, profiling and safe lifecycle handling.
