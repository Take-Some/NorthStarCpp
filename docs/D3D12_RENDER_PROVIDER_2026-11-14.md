# Direct3D 12 native provider — integration and verification
Date: 2026-11-14

## Architecture and API

The Windows D3D12 provider is an external plugin (render_d3d12.dll) that
implements the engine.render ABI 5. D3D11 remains independently replaceable.
Viewport first attempts D3D12, then D3D11 if the D3D12 module is missing,
initialization fails, or the renderer returns an error. The old DXGI swapchain
is released before creating the new one.

## Features implemented

- D3D12 hardware adapter and explicit WARP device creation.
- Command queue, list and allocator, DXGI flip-model swapchain, GPU fences,
  viewport resize, RTV/DSV descriptors and depth buffer.
- HLSL PSOs for line/triangle world geometry and depth-disabled HUD.
- Native skydome shader from the D3D11 atmospheric lighting implementation:
  YDD vertex/index data, YTD RGBA8 shader-resource texture, tint, texture
  staging with 256-byte row pitch, static sampler, rotation-only camera.
- FPS reticle, health, ammunition, recoil, pause backdrop.
- Actual GPU readback of the rendered DXGI back buffer into a CPU-mappable
  buffer, with pixel coverage assertions; an empty frame is rejected.
- Optional render memory stats ABI, sky memory residency reported and cleared.
- Provider version: 1.1.0. Renderer host ABI unchanged.

## Verified tests

Build: out/d3d12-provider-qa-20261114
Toolchain: Windows x64 / GNU GCC 15.2 / Release / Ninja

| Test | Result |
|---|---|
| northstarcpp_d3d12_warp | PASS |
| northstarcpp_d3d12_hardware | PASS |
| northstarcpp_camera_d3d11 | PASS (Viewport regression) |
| northstarcpp_d3d12_warp_stress | PASS, 1000 frames |
| Explicit hardware stress run | PASS, 1000 frames |

The native provider tests exercise sky-only and HUD-only scenes, readback,
mesh-triangle ABI, resizing, resource lifetime and GPU memory extension.
They load the D3D12 DLL directly, so cannot silently use the D3D11 fallback.

### Stress timings

CPU wall-clock per draw including Present and vertical synchronization.
These are NOT pure GPU timestamps.

| Backend | Frames | p50 ms | p95 ms | p99 ms |
|---|---:|---:|---:|---:|
| WARP | 1000 | 16.691 | 17.214 | 31.090 |
| Hardware D3D12 | 1000 | 16.665 | 16.934 | 27.032 |

## Production release requirements

The packaging system requires a full engine Release build, full CTest pass,
staged QA package, independent signed clean-machine acceptance, and approved
V8 provenance for promotion. Do not replace production dist or disable
release gates to bypass these requirements.

Full Release build was attempted but did not pass: GCC cc1plus.exe reported
out-of-memory while compiling JoltPhysics with parallel workers. A follow-up
single-worker configuration was blocked by CMake timeout under system load.
No full release package was certified or published.

Remaining checks: complete full build and full CTest, QA manifest/SDK/package
verification, real game/editor workloads and fullscreen/suspend/device-removal
stress, GPU timestamp query profiling on representative Night City frames.


## Release QA continuation (2026-11-14)

- JoltPhysics Release, two workers: PASS, 139/139 steps, exit code 0.
- Whole-engine Release, two workers: PASS, 709/709 steps, exit code 0.
- First full CTest: 96/98 passed. Failures were stale Jolt SDK path
  expectation and malformed source world_surfaces.ytd NETD mip table.
- Corrected architecture test path to JoltPhysics/Build/CMakeLists.txt.
- Original world_surfaces.ytd preserved verbatim in the source_archives
  directory and in the project's authored texture directory. The shared
  runtime catalog uses the existing validated normalized copy.
- Focused regression: 2/2 PASS.
- Full CTest rerun: 98/98 PASS, 46.14 seconds; output at
  out/d3d12-provider-qa-20261114/d3d12-full-ctest-rerun.log.

### Packaging freshness blocker

After build and tests, a concurrent workspace architectural refactor
moved compatibility source files. Reconfiguring CMake produced a Ninja
graph referencing a source that no longer exists:
engine/northstarcpp/modules/core/northstarcpp/compat/core/sources/geometry_primitives.cpp.
Ninja -n all now fails with missing input. The successful build and
CTest represent the earlier workspace snapshot, not the concurrently
modified current source tree. A stale package cannot be certified.

Shipping status: complete QA package NOT produced, production dist
NOT modified, independent clean-machine signature NOT available.

After the architecture migration completes: reconfigure a clean isolated
Release tree, build all with two jobs, rerun CTest, create and verify a
QA-only package using the official build_dist.py pipeline, and perform
independent signed clean-machine acceptance before promotion.
