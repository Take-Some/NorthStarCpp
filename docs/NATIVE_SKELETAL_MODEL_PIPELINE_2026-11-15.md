# Native Skeletal Model Pipeline — 2026-11-15

## Goal

Load native YDD/YDR models, YMAT/YTD PBR material sections, companion YMT
skeletons and original YCD animations as a single resource in Player and
Qt Editor. No OBJ conversion is used. Rendering remains behind optional,
replaceable versioned engine.render provider extensions.

## Implemented

- NativeModelResource reuses the original character YDD decoder instead of
  duplicating the v4 auxiliary 8-influence skin parsing. Checks match vertex
  positions, indices, section ordering, material references and skin counts.
- 15,535 original pistol skin vertices, 62 YMT joints, 8 mesh parts, 21,902
  triangles, 4 unique original YMAT materials with YTD texture resources.
- Strict native CPU reference skinning with normalized positions/normals and
  unchanged sections/UV/index topology. Maximum pistol bind-pose error
  7.45058e-09 units.
- Original YCD idle (one frame), p-fire and p-reload clips load, sample and
  deform vertices (maximum observed displacement 0.0469078 units). Native
  YCD can contain zero scales for intentionally hidden parts, while YMT
  bind matrices continue to require invertible scales.
- Viewport uses per-entity engine.model.animation v1 scene component,
  caches YMT joint hierarchy and YCD clips, pauses and clears animation
  state when appropriate. A missing extension or unsupported material
  uses the tested CPU skin -> existing PBR GPU rendering fallback.
- V8 script Scene.playAnimation(index, "animations/weapon/pistol/
  military-pistol.ycd@p-fire") and Scene.stopAnimation(index) route through
  engine.scene.setAnimation, with VFS reference validation, runtime
  entity lifecycle cleanup, and scene overlay rather than source mutation.
- New optional render extension in render_skinning_api.h, module ABI
  0x534b4d01. D3D11 uses GPU vertex shader skinning for up to 256 joints
  and 8 influence weights using row-major bone palette matrix buffers.
  Its output shares native UV/PBR textures, material parameters, and
  pixel shader. Original mesh remains immutable and reusable by
  multiple entities.
- D3D11 GPU path currently supports opaque, graph-free material batches;
  transparent or graph-variant materials fall back to CPU skinning.
  D3D12 and external providers without this extension retain CPU
  skinning, preserving backwards compatibility.
- D3D11 WARP GPU smoke tests actual textured GPU skin output and a bone
  matrix translation that moves geometry off screen, followed by
  successful bind-pose restoration, not merely accepting an API call.

## Verified checks

QA directory: out/native-skin-final-qa-20261115.
- Compiled native resource host, Viewport, D3D11/D3D12 render providers,
  and GPU material smoke executable.
- CTest 3/3 passed: shared assets original YDD/YMT/YCD, D3D11 WARP native
  skinning and PBR readback, D3D12 material readback.
- Earlier same-session QA Player + Qt Editor build and 8/8 integrated
  tests succeeded with CPU skinning and original V8 weapon animation.
- A fresh complete Player and Qt Editor binary incorporating the new
  D3D11 GPU skin extension has not yet been signed off; the independent
  clean build contains extensive Jolt/Bullet dependencies.

## Remaining work

- Complete fresh Player and Qt Editor binary build, run real V8 pistol
  fire/reload E2E with explicit D3D11 selection and check pixels and
  skin GPU utilization in Player. Qt GPU viewport vs software asset
  preview visual parity still requires explicit screenshot comparison.
- Implement versioned GPU skinning for D3D12 and test provider swaps,
  transparent/skinned material graphs, multi-object stress and hot reload.
- Provide a genuine production YDR fixture; none is present under
  sharedAssets, Projects or tests/fixtures. Do not claim YDR parity.
- GPU timestamps and CPU/GPU frame p50/p95/p99 over 1000+ valid frames,
  memory growth during streaming and fast travel.
- Profile and eventually avoid per-frame expanded indexed triangle uploads.

No changes to production dist. No Git commit or push performed.
