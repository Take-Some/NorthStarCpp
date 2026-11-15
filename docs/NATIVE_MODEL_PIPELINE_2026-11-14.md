# NorthStarCpp Native Model Pipeline — 2026-11-14

## Scope and source of truth

YDD/YDR are native 3D model formats. OBJ is an optional import
format, never a required conversion stage. All assets use the engine
AssetManager VFS; builtin and external providers retain ABI version
negotiation.

## Implemented

1. Native binary NEF8 YDD decoding with bounded headers, vertex
   buffers, UVs, normals, index ranges, and drawable selectors.
2. MeshAsset sections preserve the original YDD/YDR MeshPart name,
   material reference and index range.
3. NativeModelResource loads one native mesh, resolves distinct
   YMAT material definitions and YTD images once, and loads an optional
   companion .skeleton.ymt. Missing rigs are valid for static models.
4. Viewport reuses the bundle in CPU caches and submits UV/PBR triangles
   grouped by native MeshPart material. The selected renderer negotiates
   Materials v1/v2 and honors scene-level material overrides.
5. Qt Asset Preview uses YMAT/YTD base color with perspective-correct
   UV sampling and exposes native material-section and skeleton counts.
   The Qt orbit preview is a CPU approximation, not the runtime shader.
6. Invalid authored models/materials are quarantined and logged rather
   than repeatedly reparsed every frame.
7. Asset browser recognizes YDD and YDR as model types.

## Original acceptance resource

- sharedAssets/models/weapon/pistol/pistol.ydd
- sharedAssets/materials/weapon_pistol.ymat
- sharedAssets/textures/weapon/pistol.ytd
- sharedAssets/models/weapon/pistol/pistol.skeleton.ymt
- Projects/NorthStarFirstPersonPlatform/prefabs/pistol.nprefab

Verified: eight mesh parts, 21,902 triangles, four unique native
material references, nonempty decoded YTD albedo, 62 skeleton joints.

## Verification

Isolated QA build folder: out/native-model-pipeline-qa-20261114.
- Native asset and editor preview regressions: PASS.
- D3D11 and D3D12 WARP material readback: PASS.
- Player V8 Scene, Prefab and Pistol tests: PASS.
- Native Model Viewport library: compiled.
- Final independent Qt Editor binary: separately check build status.
- Installed dist and release installer are unchanged; no commit/push.

## Remaining before full native skinned-model parity

- Decode per-vertex bone indices and weights from original binary
  auxiliary attribute streams, rather than treating YMT as metadata.
- Animate original YDD components using bind transforms and clips;
  implement GPU skinning and verify deformations against reference.
- Validate an actual production YDR fixture (not currently present
  under sharedAssets), including material and rig variants.
- Add screen-level GPU PBR visual parity tests for the full Pistol
  material/normal/roughness pipeline. Existing GPU tests verify
  materials in a synthetic textured fixture; they do not guarantee
  pixel-perfect original weapon fidelity.
- Validate Play-In-Editor and authored hierarchy/component tooling
  with real skeletal models, not only asset-dialog previews.
- Add p50/p95/p99 profiling over at least 1000 valid frames and
  memory limits during streaming and hot reload.
