# NorthStarCpp — World Surface Texturing Integration

Date: 2026-11-14
Workspace: C:/Users/AidenFox/Documents/NorthStarCpp
Scope: existing engine.render ABI 5, project FPS map, NEF8 NETD YTD textures.

## Authoritative source dictionary

Original input is sharedAssets/textures/world_surfaces.ytd.
A matching original also exists in
Projects/NorthStar_FPS_Test/assets/textures/world_surfaces.ytd.
Both original files remain byte-for-byte unchanged.

Original size: 14,764,050 bytes.
Original SHA-256:
334b740916ab50e99410116f9eb3dc8ca0343f2605bca88edab65c1379078a88

The NEF8 envelope/deflate stream has a matching BLAKE3 checksum and nine
NETD v2 dictionary entries. However several NETD entry spans and
tail mip offsets are inconsistent, so the strict native decoder rejects
the original dictionary. This is a damaged INDEX, not an invalid NEF8
body hash. It is not safe to pretend all original mip chains are valid.

## Normalized runtime copy

Script:
    tools/dev/normalize_world_surfaces.py

Output:
    Projects/NorthStar_FPS_Test/assets/textures/world_surfaces_runtime.ytd

Output SHA-256:
980651c03517557d1753061dd202e99a74c8bb335f03488b99793900694667be

The script validates the original BLAKE3, NEF8 and NETD structures,
reconstructs a contiguous per-entry NETD index from actual mip bounds,
copies the original compressed BCn texture bytes without re-encoding,
and drops only damaged or non-contiguous tail mip levels. It writes
a separate file, refuses to overwrite the source, and verifies the
new NEF8 envelope, BLAKE3 and rebuilt mip index.

| Surface | Native entries | Base format | Normal / roughness | Mip counts |
| --- | --- | --- | --- | --- |
| Sand | ground_sand + _normal + _roughness | 102 BC1 sRGB | 105 BC5 linear | 12 / 10 / 12 |
| Rock/moss | rock_moss + _normal + _roughness | 102 BC1 sRGB | 105 BC5 linear | 11 / 9 / 10 |
| Forest floor | terrain_forest_floor + _normal + _roughness | 102 BC1 sRGB | 105 BC5 linear | 11 / 8 / 10 |

No original texture data has been fabricated. Missing tail mip
records are excluded from the normalized dictionary.

## Integration

- The shared native NETD parser now supports format ID 105 (BC5).
- Optional include/northstarcpp/render_surfaces_api.h extends engine.render
  without changing the main render ABI version 5. It accepts nine
  BC1/BC5 texture slots and an aligned per-cube surface ID buffer.
- D3D11 creates immutable compressed texture SRVs, binds all nine
  maps and uses world-space triplanar mapping on terrain proxy cubes.
- The shader uses authored albedo, normal and roughness data with
  simple surface lighting. It explicitly encodes the final color
  from linear to sRGB for the non-sRGB DXGI backbuffer.
- Renderer receives data via the active VFS/AssetStore, not file-system
  I/O inside the GPU provider. The host uploads texture copies once
  when project asset sources are attached.
- The current D3D12 provider does not implement this optional API;
  the host selects its existing D3D11 fallback for world-surface projects.

## FPS scene material assignment

Existing NEF8 scene:
    Projects/NorthStar_FPS_Test/main.nscn

The project contains 677 actor objects. Under current naming:
441 Deck actors -> sand;
235 Perimeter actors -> rock with moss;
1 Player actor -> not a terrain surface.
The forest-floor maps are available for future Forest/Grass actors.

The engine maps these names/layers at render time: no scene geometry,
physics, navigation, traffic or collider records were rewritten.
Authors can override the inferred surface on a specific entity by
adding the raw ECS component:
    type = engine.world.surface
    version = 1
    UTF-8 payload = one exact base color name:
       ground_sand
       rock_moss
       terrain_forest_floor

Unknown data remains preserved by the NSCN/ECS serializer.
This is an initial authoring material mapping, not a full material
graph or an automatic landscape splat-map.

## Verification status

PASS:
- Native D3D11 WARP render test northstarcpp_world_surfaces_gpu:
  3 distinct terrain surface categories, all 9 BCn texture resources
  uploaded, GPU readback with varied material colors.
- Existing native skydome test northstarcpp_camera_d3d11.
- Standalone Python normalization regression:
  tests/test_world_surfaces_normalizer.py
  validates deterministic output, nine entries, copied mip payloads,
  valid repaired index and unchanged originals.

ADDED, NOT YET CONFIRMED:
- northstarcpp_world_surfaces_fps_scene, intended to draw the complete
  677-object NSCN through the real engine WARP path.
  An unrelated parallel CMake/Ninja configure/build contention repeatedly
  exceeded tool time limits during the final rebuild, so this test must
  not be reported as passed.

Commands:
    python -B tests/test_world_surfaces_normalizer.py
    cmake -S . -B out/editor-ue-final-qa-20261114 -DCMAKE_SUPPRESS_REGENERATION=ON
    cmake --build out/editor-ue-final-qa-20261114 --target northstarcpp_camera_render_smoke
    ctest --test-dir out/editor-ue-final-qa-20261114 -R northstarcpp_world_surfaces --output-on-failure

## Limitations / next steps

- Current optional renderer is D3D11/HWND Windows. A Vulkan/D3D12,
  cross-platform extension remains to be implemented and profiled.
- Surface texturing currently applies to scene cube proxies. Existing
  OBJ triangle extension still uses vertex colors; UV/material-bound
  mesh texturing needs a distinct GPU geometry/material pipeline.
- Normal map treatment is a simplified detail perturbation and
  roughness is not yet a full metallic/roughness BRDF.
- Surface inference uses actor/layer names until a typed component
  editor and actual material associations are available.
- Full real-map visual acceptance, cold/warm residency, p95/p99 frame
  time and hardware GPU profiling are still outstanding.
- Production dist is unchanged; this is source/QA development work,
  not a release deployment.
