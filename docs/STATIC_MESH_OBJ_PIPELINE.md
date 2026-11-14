# NorthStarCpp — static OBJ mesh pipeline (initial P1 milestone)

Date: 2026-11-14  
Copyright (c) 2026 Kayla's Systems — Kayla Verner.

## Runtime architecture

The active NorthStarCpp Player/Editor now contains a first implementation of
**real static 3D mesh rendering** rather than requiring every scene object to
be depicted as a procedural cube.

| Layer | Implementation |
| --- | --- |
| Public geometry SDK | `sdk/include/northstarcpp/mesh_asset.hpp` |
| Source OBJ parser | `modules/host/mesh_asset.cpp` |
| AssetManager / virtual-file adapter | `modules/host/mesh_asset_assets.cpp` |
| Optional render API | `sdk/include/northstarcpp/render_triangles_api.h`, ABI 1 |
| D3D11 GPU implementation | `modules/render/d3d11/d3d11.cpp` |
| Scene object integration | `modules/viewport/viewport.cpp` |
| Project source/archive mounting | `modules/editor/window.cpp` |
| Integration fixture | `Projects/NorthStarAnimationLab/` |
| C++ integration test | `tests/mesh_asset_smoke.cpp` |
| GPU WARP readback/regression test | `tests/render_memory_smoke.cpp` |

## Asset format and scene usage

Source project layout:

```text
Project/
├── main.nscene
├── northstarcpp.project.json
└── assets/
    └── models/
        └── pedestal.obj
```

A Scene V3 `entity` can set `SceneNode.asset` to
`"assets/models/pedestal.obj"`. The resource is loaded through the active
AssetStore/VFS. Project mounting also works with archive-backed AssetStore
when provided with an archive path.

Supported parser records: `v`, `vt`, `vn`, `f`, object/group/smoothing
metadata, material names and references (material loading is NOT yet
implemented). Polygon faces are fan-triangulated; concave polygons are not
yet guaranteed correct. Negative OBJ indices are supported. Non-finite
positions, invalid indices, malformed faces, oversized sources and
degenerate triangles are rejected.

Bounds and triangle indices are generated in a provider-neutral C++ resource.
The current GPU path is intentionally **vertex-color/unlit**, one color per
authored scene entity, with a selection highlight in the editor. Position,
Euler orientation and scale affect world-space triangle positions.

### ABI negotiation

The primary render provider ABI remains `NORTHSTARCPP_RENDER_ABI == 5`.
Providers may optionally expose
`northstar_render_get_triangles_api(NORTHSTARCPP_RENDER_TRIANGLES_ABI)`.

`set_triangles(renderer, vertices, count)` copies caller-owned world-space
triangles immediately. Zero length removes the previous triangle geometry.
Providers lacking the optional interface retain the legacy debug cube
fallback, avoiding an ABI-incompatible upgrade.

The WARP test exercises real D3D11 triangle submission, performs a
frame readback for colored pixels, tests invalid triangle counts and confirms
the provider retains its own copy of the input buffer.

### Resource validation and export

- OBJ resource paths must be VFS-relative under `assets/models/`.
- OBJ source has an 8 MiB bound; a single OBJ has capped vertex/face counts.
- Dynamic submission has a 300,000-vertex/frame ceiling.
- The game exporter includes OBJ resources from `assets/` and validates
  referenced OBJ paths in Scene V3. Missing/redirected assets fail packaging.
- Game release manifests already hash all packaged assets.

## Verified in this development pass

- Standalone C++ parser/Scene V3 integration: **PASS**.
- Isolated D3D11 test DLL compiled and linked: **PASS**.
- Real D3D11 WARP test with colored-pixel GPU readback: **PASS**.
- C++17 syntax: mesh parser, AssetStore adapter, Viewport, Player and
  updated D3D11 provider: **PASS**.
- Game exporter regression tests: **15/15 PASS**.

## Not complete — required for production graphics

This milestone does **not** satisfy the entire P1 graphics requirement:

1. OBJ `mtllib/usemtl` are parsed as metadata only; there is no PBR
   roughness/metalness, base-color texture, normal map, image-based lighting,
   shadow map or physically based light response.
2. Indexed persistent GPU mesh buffers, shader material graphs, draw batching,
   LOD cooking and independent GPU mesh residency are not implemented. The
   initial extension copies triangulated world-space vertices each frame.
3. Mesh-aware collision is not automatically created from the OBJ resource;
   the PhysicsScene body is still the primitive authored scene collider.
4. No glTF 2.0, FBX, rig/skin or skeletal clip import through this pipeline.
5. OBJ source hot-reload and large-world eviction policies require additional
   cache lifecycle/validation.
6. The isolated DLL/WARP test proves the render extension, but **the newest
   entire Player/Editor build and interactive visual game acceptance are still
   separate gates**.

Do not claim full production mesh/PBR or published-game readiness until
clean Release, actual project run, material/physics integration and a
standalone distribution test pass.
