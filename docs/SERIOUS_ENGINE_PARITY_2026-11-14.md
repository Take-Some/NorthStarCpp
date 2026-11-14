# NorthStarCpp — Serious Engine 1.10 capability comparison

Date: 2026-11-14
Workspace: C:\Users\AidenFox\Documents\xray2
Reference: https://github.com/ptitSeb/Serious-Engine
Upstream: Croteam Serious Engine 1.10; ptitSeb's Linux/OpenPandora port.

## Method and scope

Compared the public Serious Engine repository's project inventory/README with
active NorthStarCpp source, API headers, CMake registrations, tests and existing
verification reports. Archived compatibility headers do not establish a
shipping feature. A source API without a complete end-to-end test is likewise
not production-ready.

Serious Engine ships projects for the game runtime, game logic/entities,
WorldEditor, Modeler, SeriousSkaStudio, an entity compiler, DedicatedServer,
RCon, GUI support, dependency and shader/font utilities and a masterserver.
Serious Engine 1.10 is a legacy FPS engine, not a modern rendering benchmark.
Its upstream license is GPL-2.0: this work does not copy its source code.

## Capability matrix

| Area | NorthStarCpp active source | Required end-to-end work |
|---|---|---|
| Runtime | C++ Player, scene, ECS, V8 project hooks | Clean Release build, packaged V8 startup |
| World authoring | Scene V3, streaming, spatial HLOD, editor | Prefabs, professional outliner/gizmo, large maps |
| Models and materials | Asset Manager, VFS, texture/mesh APIs | Full import/cook, material editor, thumbnails, DDC |
| Animation | Timeline, scene transform animation | Skeletal skinning, blending, IK, state graph |
| Game logic | Project-owned JS, ECS/physics scripting subset | Full ECS mutation, AI/navigation, script UI |
| Physics | Bullet/Jolt providers, collision queries | Body/constraint editor, controller tests |
| Graphics | D3D11 viewport, scene/skydome | Lighting/shadows/materials, Vulkan, GPU profiling |
| Input | Win32/XInput | Rebindable action maps and action editor |
| Audio | WinMM PCM | Spatial audio, streaming mixer, audio tools |
| Game saves | Scene file I/O | JSON save slots added; full world restore missing |
| Networking | Legacy dormant compatibility sources only | Network transport, replication, dedicated server |
| GUI | Qt project browser, native editor | Complete content tools and in-editor packaging |
| Distribution | CMake, game-dist, plugin manifests | Fully validated installer/exe, CI and rollback |
| QA | CTest, native smoke, QA reporter | Soak tests and real CPU/GPU frame timings |

## Changes made in this comparison

- include/northstarcpp/save_game.hpp — bounded public C++ save API.
- engine/northstarcpp/modules/host/save_game.cpp — JSON validation, atomic
  replacement, versioned format and corrupt-file checks.
- engine/northstarcpp/modules/editor/window.cpp — allowlisted JavaScript
  engine.save.write/read/exists/remove bindings.
- engine/northstarcpp/CMakeLists.txt — runtime source and CTest registration.
- tests/save_game_smoke.cpp — targeted native regression coverage.
- docs/SAVE_GAME.md — documented usage and limitations.

### Verified, 2026-11-14

- SaveGameStore native GCC C++17 compile: PASS.
- SaveGameStore native CRUD/error smoke: PASS.
- Player window.cpp GCC syntax check: PASS.
- Fresh Release CMake reconfiguration and 50-step CMake native-runtime/test link: PASS.
- CTest northstarcpp_save_game 1/1: PASS; existing Node project scripts smoke: PASS.
- Real packaged V8 gameplay save/load: NOT CONFIRMED.
- Installed applications and DLLs: NOT REPLACED.

## Priority execution plan

### P0 — Playable game from editor to standalone executable

1. Full V8 Player and exported-game startup, hot reload and shutdown tests.
2. Model/texture/material import, dependency graph, cook/cache and Content Browser.
3. Stable reflected components, prefab authoring, accurate selection/gizmo,
   undo/redo and extensible inspector.
4. Complete game template testing menu → gameplay → checkpoint →
   restart → save/load → quit in Editor and packaged Player.
5. Clean Release build, dependency/license review and crash testing.

### P1 — Full general-purpose game features

1. Skeletal animation, blend graph, retargeting, IK and animation tools.
2. NavMesh, pathfinding, AI logic and tiled streaming support.
3. 3D spatial sound, mixer buses, effects and long audio streaming.
4. Scripted HUD and menus, localization, input maps and accessibility.
5. Save snapshot/restore for stable ECS IDs, physics and animation with
   schema migration and a stable project UUID.
6. Modern lighting, shadowing, materials, postprocessing and profiler.

### P2 — Multiplayer and production-grade scaling

1. Networking provider, authoritative replication, interpolation/prediction.
2. Headless dedicated server, administration and multiplayer soak tests.
3. Sector-aware GPU mesh/texture residency and baked HLOD.
4. Derived data cache, shader/PSO precache, latency p50/p95/p99 for
   1000+ real frames, memory stability profiling.
5. Safe updates/rollback and release automation.

## P0 prefab implementation update — 2026-11-14

- Scene V3 subtree capture and reusable project-local .nprefab files.
- Editor Assets menu actions: export selected prefab and instantiate.
- Unique stable ID remapping, hierarchy links, relocated streaming cells,
  layer merge, bounded transactions and normal Undo/Redo integration.
- Content Browser recognizes .nprefab resources.
- Native prefab smoke registered as CTest, including actual disk roundtrip.
- Editor and Player C++ syntax compilation PASS; full GUI/release pending.
- The implementation creates baked instances. Linked prefab updates and
  per-instance editable overrides remain future work.

## Verification and release gate update

Isolated validation build: out/serious-maturity-validation-20261114.

- CMake Release configuration: PASS.
- editor_prefab_smoke built and linked: PASS.
- Prefab disk roundtrip and two instantiated hierarchies: PASS.
- GCC syntax-only checks for Player and Editor window.cpp: PASS.
- Existing save_game and new prefab CTest: 2/2 PASS.
- Existing editor_authoring full CTest and complete Editor/Player link:
  NOT CONFIRMED. Expanding the target required recompiling Flecs;
  the longer build attempt timed out. No installed binaries were replaced.
- Next gate: finish dependency build in an uncontended toolchain and perform
  real GUI actions (prefab export/import/Undo/Redo) plus game-dist playback.

## NSCN binary-scene migration (2026-11-14)

New saves, project templates, scene prefabs and exported binary games use NSCN
v1 little-endian typed serialization with version/length/CRC32 verification.
Previous .nscene text formats are import-only; original source files remain
available for rollback. New editor Save As selects .nscn.
A separate conversion CLI and project-manifest migration script are provided.
Three official sample game projects have main.nscn and updated startup manifests.
Native/Python codec compatibility, project tests and exporter tests passed.
Four NSCN-related CTests and 37 Python tests PASS. Full Player and Editor
release artifact validation is still pending.
See docs/BINARY_SCENES.md.

## Completion criteria

For every capability, require native API, usable authoring interface,
serialization, clean runtime and exported-game behavior, negative tests,
performance evidence and licensing verification. This implementation stage
does not claim complete Serious Engine parity or overall game-engine readiness.

## NEF8 World Partition VFS async range integration (2026-11-14)

Implemented optional engine.assets ABI 3 range reads, Directory VFS/FAT/DB ranges, cached NEF8 chunk index, bounded async SceneCell residency bridge, and Editor/Player update hooks. Native VFS and async smoke tests passed. Runtime ECS/physics/GPU instantiation remains eager and full installed-release verification is not yet complete. See docs/BINARY_SCENES.md.
