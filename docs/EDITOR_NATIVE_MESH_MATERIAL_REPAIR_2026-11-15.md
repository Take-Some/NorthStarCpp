# NorthStarCpp Qt Editor: native geometry and material repair
Date: 2026-11-15

## User-visible defect
The Editor's CPU "Shaded (Proxy only)" mode paints *all* authored actors
as boxes; this is expected diagnostic-mode behavior, not a real asset preview.
A saved workspace can keep that mode enabled, hiding the native GPU mode
behind a stale default. On GPU errors the Editor returned silently to proxies.
A second defect bypassed canonical material namespace remapping for character
YDD mesh sections, so the renderer tried to read nonexistent
`materials/character_abby.ymat@mXX` rather than
`characters/abby/materials/abby.ymat@mXX`.

## Changes in source
- `modules/editor/qt_editor_window.cpp`: one-time migration of historical
  Shaded=0 setting to EngineGpu mode, preserving explicit wireframe modes.
  GPU smoke now checks the native geometry/material/texture batch counts
  when a skydome YDD fixture exists.
- `modules/editor/qt_editor_viewport.{hpp,cpp}`: persistent status overlay
  explaining the diagnostic cube-only mode or native GPU initialization error;
  exposes the last frame's mesh/material/texture counters.
- `modules/viewport/viewport.{hpp,cpp}`: per-frame diagnostics for native
  mesh sections, material batches, textured batches, cube primitives and
  unresolved meshes; unavailable triangle/VFS capability is an explicit error
  rather than silently drawing a fake mesh cube. The Outliner eye's authored
  `node.visible` applies to original as well as spawned actors.
- Player actors without an authored model path are reported once and are not
  substituted with an unrelated character or a fake cube during GPU editor
  rendering. Assign the actual project character YDD/YDR/OBJ in the inspector.
  No model references are written back to scene files automatically.
- `modules/host/mesh_asset_assets.cpp`: rebase embedded material references
  for namespaced character YDD sections using the existing canonical
  `rebase_embedded_character_material` helper; original YDD bytes untouched.
- `tests/shared_assets_smoke.cpp`: regression verifies Abby 33 mesh sections
  use canonical YMAT references and resolve their materials.

## Verified
- Direct compilation of modified viewport, Qt Editor and mesh loader sources:
  PASS.
- Native mesh loader regression:
  `northstarcpp_shared_assets_smoke.exe <absolute sharedAssets path>` PASS.
  Original pistol: 21902 triangles, 8 YMAT/YTD sections. Character Abby:
  33 mesh parts, 33 material sections.
- Existing pure character namespace regression: PASS.
- Targeted C++ static libraries archived and an isolated QA EXE linked
  successfully.

## Critical unresolved regression / do not deploy
The updated Qt Editor in the current mixed incremental QA tree still fails
runtime smoke tests. Processes have terminated with Windows 0xC0000374
(heap corruption) or 0xC0000005 (access violation), sometimes after the
functional smoke reported PASS, sometimes before. The issue also occurs
with an explicitly unavailable render provider. A separate QA executable
`build/provider-parity-qa-20261115/northstarcpp_editor_assetfix_qa.exe`
was linked to avoid overwriting an open Editor, but it fails similarly.
This means *no verified editor release build* is available for this change.
The GPU upload test with native YDD/YMAT/YTD was not confirmed end-to-end.
The standard Ninja tree also has stale dependency metadata following
out-of-memory cc1plus failures; a coherent clean rebuild and memory/heap
diagnostics are required before deploying. No production binaries replaced,
no Git commit or push performed.

## Acceptance not yet met
- Native GPU editor opens without exceptions/crashes.
- Prefab child models and player character from explicit scene VFS refs
  display real mesh, exact YMAT/YTD textures and correct transform.
- All GPU providers used by projects implement required triangle/material
  interfaces or emit a clear, nonfatal error and preserve the asset reference.
- Pixel readback proves the expected model/material output in a known scene.
