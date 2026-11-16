# Physics Material ID — Jolt per-triangle surfaces (2026-11-15)

## Implementation

NorthStar now propagates authoring-time material classification to physical
collision hits and the default player footstep event.

```text
MeshAsset.sections[].material_reference / NCM2 triangle_materials[]
    -> NorthStarPhysicsSurfaceApi v1 -> Jolt MeshShape per-triangle user data
    -> PhysicsCastHit {object_index, triangle_index, material_id, normal}
    -> engine.player.ground() -> player.footstep -> engine.audio -> NEF8/NAC
```

The original `NORTHSTARCPP_PHYSICS_ABI=4` and
`NorthStarPhysicsCastHit` layout are **unchanged**. The optional extension
`NORTHSTARCPP_PHYSICS_SURFACE_MODULE_ABI=0x50485301` is exposed both as a
module query and `northstar_physics_get_surface_api(1)` function.
It offers per-triangle material setup for authored ComplexMesh bodies,
material-aware registration for standalone triangle meshes and an extended
sphere sweep reporting the hit triangle and physical-material ID. Older
providers still use ABI 4; their material ID is 0 (unknown), retaining
existing fallback surface inference.

Jolt `MeshShapeSettings::mPerTriangleUserData=true` and each source triangle's
original `mUserData` ordinal preserve the authoring triangle through internal
BVH reordering. The extended cast resolves `mSubShapeID2` with
`MeshShape::GetTriangleUserData`, then looks up the matching
`triangle_materials` table. The material is selected from the actual
collided triangle, not based on the overall object name. Invalid count or
unknown material IDs are rejected at the provider boundary.

## Stable physical material IDs

| ID | Surface | Audio |
| ---: | --- | --- |
| 0 | unknown (fallback) | collider-level tag, then stone |
| 1 | dirt | `audio/footsteps/dirt.nac` |
| 2 | grass | `audio/footsteps/grass.nac` |
| 3 | metal | `audio/footsteps/metal.nac` |
| 4 | mud | `audio/footsteps/mud.nac` |
| 5 | snow | `audio/footsteps/snow.nac` |
| 6 | stone | `audio/footsteps/stone.nac` |
| 7 | water | `audio/footsteps/water.nac` |
| 8 | wood | `audio/footsteps/wood.nac` |

`physics_surface.hpp` contains the canonical IDs and material-name mapper.
For example, a recognized `asphalt` or `concrete` section maps to stone,
while `metal` and `grass` sections map to their corresponding categories.
Unknown names **remain unknown**, not an invented surface material.

## Scene authoring compatibility

`collision_mesh::Mesh::triangle_materials` stores one ID for each source
triangle. `NCM2` appends its 32-bit triangle material IDs after the original
NCM1 vertex/index payload. The original `NCM1` serialization remains in
use for meshes without per-triangle material assignments. Both versions can
be read; scene component lookup accepts versions 1 and 2.

When a source model exposes `MeshAsset.sections`, the collision bake maps
known section material references to individual triangles automatically.
Explicitly authored triangle IDs take precedence. The editor and original
collision importers should preserve section ranges to retain this feature.

The player ground sweep prioritizes `hit.material_id` when nonzero and
exposes `materialId` and `triangleIndex` through the script API.
Fallback order remains collider layer surface tag, material hints in object
name/asset reference, then stone for unknown ground. The root event now
includes the optional physical material and triangle ID, while the
NAC subscriber continues to use the normalized surface string.

## Verification

- `northstarcpp_physics_surface_smoke` builds and runs using real Jolt.
- `northstarcpp_jolt_triangle_materials`: PASS — grass and metal on
  adjacent quads of one standalone mesh; wood and snow on adjacent quads
  of one scene-authored ComplexMesh.
- NCM2 encode/decode and full scene serialize/parse material retention: PASS.
- Wrong triangle material count: rejected.
- Full Jolt provider and Player build in `out/collision-vhacd-qa`: PASS.
- Existing Jolt physics, player scriptless root and root event tests: PASS.
- `tests/player_footsteps_runtime_smoke.py`: PASS, real Player to physics
  contact, event and NEF8/NAC audio voice.
- Existing `footsteps_nac_audio_native` and WinMM fallback tests: PASS.

## Remaining acceptance and performance constraints

This supplies **precise per-triangle selection only where physical material
IDs have been authored or inferred from real model material sections**.
An older NCM1 collider or collision geometry with no usable material
references still falls back to the collider-level tag. Importing native
per-triangle physical material metadata from all third-party YBN-like
collision formats is not yet guaranteed. Multimaterial terrain streaming
and editor triangle-painting UI need their own acceptance checks.

Per-triangle user data increases Jolt mesh-shape memory; large city scenes
require profiling of memory, cook cost and contact-query p95/p99. The
extension is optional, does not modify ABI 4 and leaves the inactive Bullet
source tree untouched. Production/installed runtime DLLs were not replaced.
No Git commit or push was made.
