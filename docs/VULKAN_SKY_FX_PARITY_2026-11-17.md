# Vulkan sky / weapon particle first-shot hitch — 17 November 2026

## Root causes

1. `render/vulkan/renderer_frame.cpp::set_sky` rejected every nonnull `NorthStarSky` with "Vulkan textured skydome is not implemented yet". D3D12 already had a custom renderer-specific sky pass, while Vulkan's generic Materials V2 handles textured vertex batches.
2. `viewport_weapon_fx_sprites.hpp` submitted the flash, spark and smoke material batches ONLY when each emitter had live particles. The first shot therefore changed the material batch count and introduced texture atlas and blend PSO resources on the critical gameplay frame.
3. Vulkan's `upload_material_textures` can use `vkDeviceWaitIdle`, and `upload_material_texture` synchronizes via `vkQueueWaitIdle` on newly uploaded RGBA textures. Re-introducing the three sprite materials on first shot caused a noticeable freeze.

## Changes

- Sky mesh/indices and RGBA8 YTD are now copied once from the standard `NorthStarSky` contract into a retained, **unlit** Vulkan material source. The sky uses the same native source assets as D3D12 (not a recreated procedural approximation). The renderer projects vertices camera-relative, and sky fragments receive background depth, without an external sky-only model format.
- The sky is appended to Vulkan's material batches but excluded from user material shader-reference indices. It stays resident and is not cloned each frame.
- Sprite batches for flash, sparks and smoke are **persistent**. Inactive emitters submit one degenerate off-screen triangle with the unchanged atlas texture/material, keeping material counts, shader variants, descriptor keys and texture data stable as shots begin/end.
- `renderer_frame_diagnostics.cpp` was added to the Vulkan CMake target after a new diagnostics component's declarations produced unresolved externals.

## Validation

- `cmake --build out/build --target northstarcpp_render_vulkan northstarcpp_player -j 4`: **PASS** after CMake correction.
- Vulkan Player `Projects/AbbyPistolDemo/main.nscn` remained running for 13 seconds with the new sky path and **no runtime project/render exception**. Source: `out/qa/abby_vulkan_sky_particles_afterfix_20261117.log`.
- CTest: Vulkan hardware, Vulkan transparency, Vulkan GPU-profile and Vulkan render graph synchronization **PASS** (4 tests).
- `fps_weapon_effects` smoke initially failed on an out-of-date expectation: smoke starts fading in over ~95 ms, but the test required it at t=0. The assertion was corrected to check after 120 ms. Rebuilding this one test was subsequently blocked by a concurrent `northstar_shared.nsp` archive file lock (`WinError 5`) caused by other workspace activity; this re-run is **not yet certified**.

## Remaining work

- Pixel-by-pixel same-camera comparison between D3D12 and Vulkan and quantitative FPS/1%-low/p95/p99 measurements while repeatedly firing. Successful startup is *not* proof of complete visual or temporal parity.
- Lighting/HDR tone mapping differences can independently keep world geometry darker in Vulkan. The textured skydome now renders, but world lighting/exposure parity has not been fully verified.
- Vulkan still has synchronous waits during *new* material/texture introduction; persistent firearm sprites specifically remove this first-shot trigger but do not replace a future asynchronous upload scheduler and descriptor retention.
- No production dist replacement or git commit/push was requested/performed.
