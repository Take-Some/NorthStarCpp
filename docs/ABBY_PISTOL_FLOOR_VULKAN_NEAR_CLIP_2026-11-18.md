# AbbyPistolDemo — Vulkan floor near-plane and Deck repair

Date: 2026-11-18.

## Failure reproduced
- The original firing range contained 25 adjacent 10 m `Deck` cubes. Each neighboring pair duplicated coplanar internal cube faces.
- Vulkan `renderer_frame.cpp::Renderer::collect_vertices()` projected each vertex and rejected the **entire** triangle whenever even one vertex was within or behind `camera_near_clip`. This discarded giant pieces of the floor when the eye moved close to the surface.
- The Vulkan ID-pick `renderer_pick_sources.cpp` used the same invalid per-vertex reject and could lose corresponding selectable world geometry.
- The native Vulkan vertex shaders accept homogeneous `vec4 clip_position` as `gl_Position`. Correct clipping is provided by Vulkan's fixed-function primitive clipper against z=0 and z=w. The existing LESS/depth-write/clear-to-1 standard-depth pipeline was already internally consistent; it was not modified.

## Source and content changes
- `Projects/AbbyPistolDemo/main.nscn` — changed only the 25 named Deck entities into a single static 50 m cubic platform, bottom y=-50, top y=0, stable ID 2. Preserved all unrelated component payloads and stable entity IDs. 56 -> 32 NEF8 records; 45 -> 21 entities.
- `Projects/AbbyPistolDemo/main.nscn.before-continuous-floor-20261118.bak` — byte-preserving old scene backup.
- `tools/dev/repair_abby_pistol_floor.py` — idempotent binary scene repair with checksums and unrelated-record invariant.
- `tools/create_abby_pistol_demo.py` — future newly created project gets the unified static floor; verifier accommodates updated entity count and engine-owned `Factory.weapon`.
- `engine/northstarcpp/modules/render/vulkan/renderer_frame.cpp` — remove the invalid CPU near-plane per-vertex reject for both scene triangles and cube triangles; use GPU homogeneous primitive clipping. Debug-only mode 9 validates green pixels from geometry crossing the near plane.
- `engine/northstarcpp/modules/render/vulkan/renderer_pick_sources.cpp` — same primitive clipping semantics for world ID picking.
- `tests/render_vulkan_smoke.cpp` — validates near-straddling green triangle and the exact bottom-anchored 50x50 m test floor, then 120 GPU camera-motion frames.
- `tests/abby_floor_geometry_smoke.py` — validates one static floor and intact authored Pawn, target, motor, camera and skeletal nodes.

## Verified
- `python tools/create_abby_pistol_demo.py --verify` — PASS: 32 records, 21 scene entities, 10 player components, shared skydome and 29 pistol NAC tracks.
- `python tests/abby_floor_geometry_smoke.py` — PASS: one floor, no Deck seams, mass=0, top surface at y=0; targets and SkeletalMesh preserved.
- `cmake --build out/build --target northstarcpp_render_vulkan` — PASS, current `out/build/render_vulkan.dll` linked.
- `out/build/northstarcpp_render_vulkan_smoke.exe out/build/render_vulkan.dll` — PASS: actual Vulkan GPU surface/near-plane/depth/readback tests including 120 sequential camera positions with zero missing central ground pixels.
- `NORTHSTARCPP_QA_RENDER_PROVIDER=vulkan` with `tests/weapon_live_ads_alignment.py` — PASS: actual Vulkan Player, zero nonzero exit, stable ADS camera/iron sight alignment, preserved muzzle socket.
- A separately attempted 278-frame D3D12 **software WARP** locomotion run timed out before its marker; renderer and IK initialized, but that specific long-run test is NOT claimed as PASS.

## Deployment note
A currently running Editor/Player retains its loaded old renderer DLL. Exit and reopen it, using current `out/build/render_vulkan.dll` and the revised `Projects/AbbyPistolDemo/main.nscn`. This is a local source and build repair; production `dist` not replaced, no Git commit/push.
