# NorthStarCpp — full-scale game-engine readiness audit

**Date:** 2026-11-14  
**Repository:** `C:\Users\AidenFox\Documents\xray2`  
**Scope:** Active NorthStarCpp C++17 engine (not Legacy NewEngine or NewViso).  
**Policy:** A source file, API declaration or mock test is not evidence of a
complete production feature. Runtime and distributable output must be checked.

## Executive result

NorthStarCpp is a **functional engine prototype and SDK**, not yet a
production-ready full-scale 3D game creation environment. Its strongest active
areas are module interfaces, scene documents, physics/ECS basics, camera,
project scripting, asset VFS and initial editor workflows. The most consequential
missing features are general 3D mesh rendering, complete PBR materials, rigged
character animation, full import/cooking, navigation and AI, prefab/serialization,
and validated release distribution.

This audit is based on current active headers/sources/CMake targets and the
existing migration and editor ledgers, not on relocated legacy source counts.

## Capability matrix

| Area | Active capability / evidence | Gap / acceptance status |
| --- | --- | --- |
| Foundation / Math | C++ math, geometry, primitive meshes, ray/AABB, modular CMake target | Add full work scheduler, world profiling and independent SDK stress tests |
| Renderer | `engine.render` D3D11 provider ABI 5, basic cube/sky scene, camera projection and culling | Generic mesh vertex/index resources, PBR materials and arbitrary object geometry not established end-to-end |
| Rendering quality | Sky geometry and texture, camera-based frustum tests, render memory API | Directional/point/spot lights, shadow maps, GI, reflection probes, transparency, decals, particles, anti-aliasing, tone mapping and GPU profiling |
| Import / Assets | Directory + FAT/DB AssetStore, YDD/YTD-related loaders, streaming and priority budgets | Complete authoring import/conversion, mesh/material dependency cooking and editable asset previews need integration tests |
| Scene graph | Scene V1/V2/V3, stable IDs, parent/layer/cell/room, volumes, portals, LOD metadata | Prefabs, reflection-based component serialization, referenced subscenes, world-origin strategy, authorable distributed worlds |
| World partition | Spatial index, cell load decisions, two-tier approximate HLOD, collision residency extension | Real HLOD mesh assets and GPU release, object lifecycle contract, streaming integration tests in large world |
| Physics | Bullet/Jolt contracts, rigid bodies, impulses, sphere casts, static triangle meshes, residency | Convex/compound shapes, joints/constraints, character controller, triggers, cloth, ragdoll, physics materials and editor tools |
| ECS | Flecs provider ABI v2, scene position/name/visual/body/controller/motion/script components | Schema registry and reflection, arbitrary component serialization, migrations, scheduling, system dependency graph |
| Scripting | V8 native provider, ABI 2, project JavaScript, ECS/physics/scene APIs, event bridge | Build/launch of full integrated Player, real script-enabled exported game, debugging/source maps, scripts in long-running large worlds |
| Animation — camera | CameraDirector and serializable cinematic tracks, native camera timeline tool | Integrated cutscene/sequencer editing and scene-wide timelines |
| Animation — scene | **New:** bounded CPU scene-transform clips, playback state, VFS assets, JS API | Native Player visual preview not yet accepted; rotation and scale collision remain visual only |
| Animation — characters | Historical 186 animation-source files relocated | No verified active skeleton loader, GPU skinning, blend tree, retarget, IK, root motion, state machine or rigged-character game test |
| UI / HUD | Basic Win32 editor widgets and FPS prototype HUD | Production UI toolkit, layout, focus/navigation, DPI scaling, localization, textured widgets, independent UI render pass |
| Audio | `engine.audio` / WinMM PCM16 playback and project WAV requests | Spatial audio, streaming, voice limits, buses/mixer, effects, attenuation, occlusion, authorable sound graph |
| Input | Raw keyboard/mouse/gamepad and project-first scripted controls | Rebindable action maps, platform-neutral input actions, persistence and controller UI |
| AI / Navigation | No active provider-neutral navmesh runtime identified in current modules | Navigation mesh baking/streaming, pathfinding, agents/avoidance, decision trees/behavior logic |
| Networking | Historical network source group relocated | No production multiplayer transport, prediction/rollback, entity replication or dedicated server |
| Savegame | Scene authoring serialization and project assets | Versioned runtime save/load, ECS state snapshots, migration, streaming rehydration and recovery |
| Editor | Project Manager, Content Browser, inspector, native viewport, gizmos, Undo/Redo | Material editor, animation editor, prefab authoring, component reflection, navmesh tools, scripted inspection and full interactive stress tests |
| Export | Engine/game packaging scripts, manifest/checksums, V8 deployment checks | Clean end-to-end Release build, actual generated game playthrough, clean machine install, licensing & dependency closure |
| Profiling/QA | Many CTests, some smoke and memory-provider checks | Cross-subsystem profiling, ≥1000 frames p50/p95/p99, stress scenes, crash tests, asset cold/warm and a real production-game vertical slice |

**Historical source caveat:** `engine/northstarcpp/modules/foundation/northstarcpp/compat`
contains relocated code that is *not automatically compiled or linked*.
Port one contract at a time; never claim historical source presence means
feature completion.

## P1 static geometry milestone (subsequent integration)

- Added mesh_asset.hpp, bounded native OBJ importer and VFS adapter.
- Added optional D3D11 triangle submission extension, retaining render ABI 5.
- Player and Editor Viewport now load referenced assets/models/*.obj meshes,
  use authoring transforms and display actual unlit triangles rather than
  primitive cubes when the provider supports the extension.
- NorthStarAnimationLab contains an OBJ mesh instance in Scene V3.
- Isolated native parser and real D3D11 WARP pixel-readback tests pass.
- Game package export rejects missing OBJ asset references; packaging tests
  pass with 15/15.
- NOT YET: PBR material/lighting, mesh cooking, real mesh collider, glTF,
  rigged animation, clean Player/Editor Release or published game acceptance.
See docs/STATIC_MESH_OBJ_PIPELINE.md.

## Changes delivered in this pass

1. Added a provider-neutral transform animation SDK:
   `include/northstarcpp/scene_animation.hpp`.
2. Implemented bounded keyframe parsing, stepping/linear interpolation,
   clip duration, speed, loop, pause/seek and state in
   `modules/host/scene_animation.cpp`.
3. Added AssetStore/VFS adapter in `scene_animation_assets.cpp` with
   a 1 MiB pre-read limit.
4. Bound `engine.animation.play/stop/pause/seek/state` into project scripts.
   Animation moves static physics bodies and applies rotation/scale to
   visual snapshots without modifying the authored scene document.
5. Added source-project demonstration:
   `Projects/NorthStarAnimationLab/main.nscene`,
   `scripts/main.js`, `animations/lift.anim.json`.
6. Extended game exporter to stage animation clips, validate schema and sizes,
   and report/hash `animation_files` in the release manifest.
7. Added/expanded C++ and JavaScript smoke tests and exporter regressions.
8. Added `docs/SCENE_ANIMATION_RUNTIME.md` with API and limitations.

## Production completion plan

### P0 — Make current runtime reproducible and releasable

**Blocking.** Build current Editor, Player and mandatory modules in an
uncontended clean Release tree; verify provider registry and ABI negotiation;
load a real project with the actual V8 DLL; verify project startup/update/stop,
physics scene effects and clip animation; package and launch the resulting
standalone game with all imports and licenses. Run a complete CTest matrix.
An isolated V8 provider smoke already passed in the development V8 build
(`docs/SCRIPTING_VERIFICATION_2026-11-14.md`) but is *not* a standalone
Player integration test.

**Acceptance:** no unresolved imports, no missing assets, reproducible
clean build, exit codes 0, real frame output and verified project gameplay.

### P1 — General-purpose 3D content pipeline

Implement scene mesh instances with vertex/index buffers and per-object
materials, normalized coordinate conversions, mesh bounds and culling,
importers (at minimum a supported open interchange format), texture import,
LOD and dependency cooking. Link the renderer to mesh resources instead of
only debug cubes, preserving a versioned renderer ABI.

**Acceptance:** import a textured production-scale architectural scene with
multiple mesh/material types, render correctly, unload GPU allocations and
reopen the project without broken links.

### P1 — Full character animation

Skeleton/skin asset importer, clips, blend tree/state machine, GPU/CPU skinning,
retargeting, root motion, crossfade, IK and animation events; expose engine.animation
to entities, not hardcoded character paths. Integrate animation authoring into
the editor. Preserve collision and world-transform consistency.

**Acceptance:** at least one imported rigged NPC transitions between idle,
walk, run and interaction; animations play on real mesh skinning without
geometric glitches, memory leaks or invalid physics/scene state.

### P1 — Scene authoring and extensible ECS

Prefab instances/overrides, reusable scene assets, world partition tooling,
component reflection and versioned persistence, navmesh/collision visualization,
data layers, trigger/event authoring, proper undo/redo and Play-in-Editor isolation.

**Acceptance:** construct and duplicate a multi-room level using reusable
prefabs and custom components; reload and package it with exactly preserved
behavior and spatial transforms.

### P2 — Game subsystems

NavMesh and dynamic obstacle avoidance, ECS-driven AI, audio mixer and spatial
sound, input action maps, game UI system, game save/load, particle/VFX, lighting
and shadow tools. Keep per-project gameplay in JavaScript; C++ remains the
implementation of general services.

**Acceptance:** a real test game has NPC movement, interaction, menus,
music/SFX, persistence and no native hardcoded quest or game rules.

### P2 — Production graphics and large-world scaling

Extend D3D11 current renderer or introduce a properly versioned optional Vulkan
provider; PBR/IBL, shadows, reflection/lighting, postprocessing, GPU profiling,
mesh/texture streaming and HLOD cooked meshes. Measure CPU/GPU separately.

**Acceptance:** 1000+ representative frames with valid p50/p95/p99
frametimes and memory usage under a documented quality preset; no orphan GPU
resources after streaming and scene transitions. A 60 FPS target corresponds
to ~16.67 ms/frame but requires hardware/preset-specific benchmarks.

### P3 — Distribution, portability and network-ready architecture

Validated release installers, crash symbols, API documentation and SDK samples,
rebuild-from-clean-CI, asset licensing, multiplayer/network replication if
required, dedicated server mode and security isolation for untrusted scripts.

**Acceptance:** a clean-machine install successfully runs a self-contained,
signed/verified game and regenerates equivalent project builds.

## Today's verification

- C++ native scene animation smoke: **PASS** (standalone link), including
  real `NorthStarAnimationLab` Scene V3 and clip asset.
- Player/Editor and animation C++17 syntax checks: **PASS**.
- JavaScript Animation Lab/gameplay contract smoke under Node: **PASS**.
- Game packaging tests: **14/14 PASS** including schema, timing, scale,
  bounds, hashes and packaging integrity., including clip integrity and invalid
  format rejection.
- CMake regeneration: **PASS**.
- Full combined CMake target `northstarcpp_scene_animation_smoke` linking:
  **not confirmed** due competing Ninja builds and reprocessing of the
  validation tree. Standalone native test above succeeded.
- Full Editor/Player Release, GUI image comparison, native V8 game runtime and
  packaged game launch: **not executed/verified in this pass**.

## Release decision

**NOT PRODUCTION READY.** The current engine has working subsystems and a new
animation foundation, but broad content and full-game integration gates are
open. Do not publish a statement of complete production parity with large
commercial game engines until the P0–P2 acceptance artifacts exist.
