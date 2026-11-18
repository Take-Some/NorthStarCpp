# NorthStarCpp — Current Engineering Status

Updated: 2026-11-18. This page is the **status entry point**, not a replacement for system specifications or historical QA reports. Results below refer to the identified local build and source state, not a fully released product.

## At a glance

| Area | Current evidence | Not established |
| --- | --- | --- |
| Core engine | C++ runtime, Player and Qt Editor linked in the isolated local QA build | Clean-room distribution and complete compatibility matrix |
| Gameplay / player | Controller events, Pawn transfers, versioned CharacterMotor bridge, Jolt integration; focused smoke QA | Full Unreal parity, multiplayer ownership, complete movement-state transitions |
| Camera | View Target, self-collision, deleted-target cleanup, streaming camera QA, D3D12 image readback | Real KaylaNightCity third-/first-person visual acceptance on all providers |
| AI navigation | Local NavMesh, physics movement, route replan on changed goals | Polygonal multilevel NavMesh, crowd avoidance at scale |
| Animation | Skeletal/animation tooling and targeted smoke tests | GPU acceptance with Ellie, Isaac, Abby and full root-motion/IK continuity |
| Assets | Build-time registry/DDC metadata, NEF8 XML YMAT only, deterministic NSP packing | Unified C++/Qt registry ABI, binary NEF8/prefab dependency closure and cooked GPU DDC |
| Rendering | D3D12-focused capture/test paths, other providers under development | D3D12/Vulkan feature and performance parity, production GPU budgets |
| Release 1.0 | A staged plan and smoke acceptance targets exist | Full regression, QA of a distributable Night City game and clean-machine release gate |

## Reproducible local verification

On 2026-11-18 the separate QA build under out/q7 successfully linked
the native Player and Qt Editor and passed a targeted gameplay, navigation,
camera, asset and Qt authoring subset. This is NOT full-regression or
release acceptance. The exact test names, sample counts and CPU draw-wall-time
percentiles are maintained once in
[Gameplay Production Gates](GAMEPLAY_FRAMEWORK_PRODUCTION_GATES_2026-11-18.md).
Those CPU draw timings are not GPU execution timestamps or Night City FPS.

The historical [1.0 Execution Plan](NORTHSTARCPP_1_0_EXECUTION_PLAN_2026-11-18.md) records failed tests from a different `out/build` snapshot. Those older results must not be silently overwritten with the `out/q7` outcomes. A new clean-build QA matrix is required for release acceptance.

## Authoritative details and risks

Gameplay architecture and the specific outstanding work: [Gameplay Production Gates](GAMEPLAY_FRAMEWORK_PRODUCTION_GATES_2026-11-18.md). The earlier [UE5.3 alignment](GAMEPLAY_FRAMEWORK_UE53_ALIGNMENT_2026-11-18.md) and [runtime phases](GAMEPLAY_FRAMEWORK_RUNTIME_PHASE2_2026-11-18.md) are historical snapshots.

Canonical YMAT is NEF8 kind 4 with XML; kind-30 JSON YMAT is no longer accepted. Distinct Material Function and Material Instance JSON asset types still exist; they are **not** legacy YMAT. See [XML YMAT Migration](YMAT_XML_ONLY_MIGRATION_2026-11-18.md).

Asset Registry identity and deterministic NSP packaging are partly integrated at build time. The shared input inventory and byte counts vary as assets change; never treat a historical `224` or `223` count as a permanent format invariant. See [Asset Pipeline P1](ASSET_PIPELINE_P1_FOUNDATION_2026-11-18.md) and run its verification before shipping.

Rendering feature parity, full scripted interaction, real-world GPU/animation acceptance, stable moving-platform rider synchronization, crowded urban NPC behavior, and end-to-end release packaging remain open gates. These must be tested on current source and project assets.

## Where to work next

Use [Quick Start](START_HERE.md) for the canonical preset build, [Documentation Index](INDEX.md) for subsystem references, and [1.0 Execution Plan](NORTHSTARCPP_1_0_EXECUTION_PLAN_2026-11-18.md) for acceptance criteria.

A successful targeted test run confirms only the tested slice. Do not mark the entire game, render backend or engine production-ready without complete evidence.
