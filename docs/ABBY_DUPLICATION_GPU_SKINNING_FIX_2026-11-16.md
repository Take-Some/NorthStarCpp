# Abby duplication and FPS remediation — NorthStarCpp (2026-11-16)

## Root cause
AbbyPistolDemo contains one authored SkeletalMesh (Entity ID 44, Pawn ID 1). The engine's packaged/embedded API copy lacked Scene.handleByName, so the project script silently instantiated another Abby prefab at runtime. This rendered the same high-detail character twice.

## Implemented
- Added Scene.handleByName (nonowning authored Entity handle) and rotateStatic to the shipped JavaScript API and matching TypeScript declarations.
- Project script and demo template now reuse authored SkeletalMesh and refuse to spawn another model when the API is outdated. Only a genuinely missing authored visual permits fallback instantiation.
- Removed per-frame duplicate Pawn→SkeletalMesh position writes; rotation is sent only when heading changes.
- Allowed authored noncolliding SkeletalMesh to enter first-person visual mode through native scene.setVisualMode; introduced guarded PhysicsScene authored-visual rotation, without changing Pawn physics.
- Preserved source YDD geometry, UV, materials, and all eight bone weights. Added immutable GPU skin vertex-section caching; FPS arms can now use GPU skinning with triangle-level arm filtering, rather than full CPU deformation. Editor GPU picking stops excess CPU skinning after its finite pick budget.
- D3D11: replaced 96 MiB clear-all texture cache with a GPU-memory-aware cache and idle-resource eviction. GPU skin source vertex buffer uploads are skipped when unchanged; animated bone palettes still update.

## QA results
- PASS: Full QA Ninja builds of northstarcpp_player.exe, northstarcpp_editor_qt.exe and render_d3d11.dll.
- PASS: QA northstar_shared.nsp repack with the corrected engine APIs.
- PASS: node tests/abby_pistol_demo_smoke.cjs (one authored Abby across FPS/TPS, only pistol instantiated).
- PASS: python tools/create_abby_pistol_demo.py --verify (44 authored entities, 54 NEF8 records).
- PASS: Native V8/Jolt/D3D11 Abby runtime smoke with temporary test-only HUD suppression, WARP and no-forced-WARP runs. Exactly 45 objects after startup: 44 authored + one runtime pistol. Five frames, animation, socket attachment, exit all pass. Production project script retains HUD.
- PASS: git diff --check on tracked affected paths.

## Not yet accepted
- Full native HUD-enabled D3D11 run is blocked by missing GPU UI compositor. Explicit D3D12 run is blocked by renderer character/material API parity; automatic fallback is suppressed by the QA provider override.
- No real-GPU stable FPS or p50/p95/p99 frame time before/after benchmark. Cold Abby 33-section YDD/material load measured approximately 2.9 s in QA; this is NOT steady-state FPS.
- No production Editor/Player replacement and no commit/push; only the QA build under C:\Users\Kayla\Documents\NorthStar\cache\northstar_toolbar_qa was created.
