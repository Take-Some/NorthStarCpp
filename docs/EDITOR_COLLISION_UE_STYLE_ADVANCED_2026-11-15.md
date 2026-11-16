# NorthStarCpp — UE-style advanced collision authoring

**Date:** 2026-11-15
**Workspace:** `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`

## Implemented
- Details > Physics | Collision Shape: **Box**, **Sphere**, **Capsule**, **Complex Mesh (exact, static)**, **Auto Convex**.
- **Object Type** classification: WorldStatic, WorldDynamic, Pawn, PhysicsBody, Vehicle, Destructible.
- **splitsNum**: 1..16 maximum convex parts; displayed for Auto Convex, saved with every collider.
- **Generate Collision From Mesh** loads the active OBJ/YDD/YDR scene model through the current AssetStore VFS; bakes a bounded, validated indexed triangle buffer into an ECS scene component.
- Prefab collision editor exposes identical controls; generated topology and metadata persist in .nprefab and survive instancing with stable entity-ID remapping.
- Selected complex collider renders the authored triangle mesh wireframe in Qt Proxy and GPU viewport overlays; view is independent of rendering materials.
- Dynamic actors cannot use exact triangle-mesh physics; Auto Convex builds one or more convex hulls.
- Both Bullet and Jolt expose the optional `NORTHSTARCPP_PHYSICS_MESH_MODULE_ABI` extension; baseline physics ABI 4 and existing providers remain loadable.

## Persistent format
- `northstar.physics.collider` version 2 stores shape, XYZ dimensions/offset, splitsNum, Object Type. Version 1 remains readable.
- `northstar.physics.collider.mesh` version 1 stores source reference and endian-stable vertex/triangle topology.
- Collision mesh validation: at most 18,000 vertices, 54,000 indices, 900,000 binary bytes. It rejects malformed indices, nonfinite coordinates, and stale model references.
- World-space transformation (object size, non-uniform scale, Euler rotation, local offset) is applied when the physics world loads, not baked permanently; reassigning the model requires regenerating the mesh.
- `.nprefab` text V3 stores component payloads as hex; source prefab limit 4 MiB is checked on save.

## Tests
- Direct C++ object compilation: host physics, Jolt, Bullet, Qt editor, Qt asset preview, Qt GPU viewport, editor prefab tests: **PASS**.
- `northstarcpp_physics_collisions_qa.exe` with Bullet and Jolt: **PASS (exit 0)**. Covers static complex mesh sweep, dynamic Auto Convex sweep, disabled collision, Object Type v2 round trip, and exact mesh wireframe geometry.
- `northstarcpp_editor_prefab_smoke.exe`: **PASS (exit 0)**. Covers collider v2, binary mesh copying, .nprefab/Scene V3 serialization, and stable entity ID remapping.
- QA editor binary `northstarcpp_editor_collisions_ue_qa.exe`: **LINK PASS**; the headless `--smoke` remains unverified because even the previous editor binary now hangs during early startup in this QA environment. Do not treat the GUI as fully accepted without an interactive check.

## Remaining differences from Unreal Editor
- `splitsNum` is currently a deterministic triangle-bucket / convex-hull approximation, **not** Unreal's voxel-based VHACD and not guaranteed to produce exactly the requested number of hulls.
- Object Type is stored and accessible to the physics provider, but channel-specific Block/Overlap/Ignore response masks and per-channel query filtering are **not yet implemented**.
- Complex Mesh wireframe shows source mesh edges for the selected object (capped at 3,600 segments per frame). Auto Convex wireframe currently displays source triangles, **not cooked hull boundaries**, so it must not be treated as a pixel-exact visualization of generated convex shapes.
- Bake runs synchronously; very large meshes are rejected, not simplified or cooked in the background.
- No full interactive GPU screenshot/acceptance test or large-scale profile completed.
- The active `northstarcpp_editor_qt.exe` has not been replaced; changes are source-local plus isolated QA binaries. No Git commit/push was performed.
