# NorthStarCpp 1.0 — release execution and acceptance plan

> **Document status:** Release exit criteria below remain authoritative. The initial failing-test list records an earlier out/build snapshot, NOT the later isolated out/q7 results. Refer to [Current Status](CURRENT_STATUS.md) before quoting results as a present-day release verdict.

Date: 2026-11-18
Scope: source repository `NorthStarCpp`, KaylaNightCity vertical slice, standalone engine/editor SDK.
Policy: **do not declare stage completion solely from a CTest smoke pass**.

## Initial verification (local existing `out/build`)

The inspected tree had **359 modified tracked files** at the time of the
audit. These are pre-existing, uncommitted user changes. The tests below
exercise binaries already in `out/build`, not a proven clean rebuild of
the current source tree. **No source, build artifact, dist, commit, or push
was overwritten by the audit.**

- `northstarcpp_animation_graph`: PASS.
- `northstarcpp_player_action_events`: PASS.
- `northstarcpp_player_blueprint_physics`: FAIL
  (`Valid Root Motion rejected tick=1`).
- `northstarcpp_viewport_skeletal_pipeline`: FAIL in the stage-1 gate.
- `northstarcpp_editor_prefab`: FAIL (test exit 31). This exit branch checks
  canonical weapon-prefab properties, not merely whether prefab parsing works.
- `northstarcpp_prefab_composition`: FAIL in the stage-2 gate.
- `northstarcpp_project_script_assets`: FAIL in the stage-2 gate.
- Stage-3 selected CTest subset: PASS; **the playable game is not accepted**.
- Stage-4 selected CTest subset: PASS; **performance and stability are not accepted**.
- Stage-5 focused run: no verdict (process timed out during tool execution).

Existing project notes document earlier Player/V8 runtime inconsistencies,
live-animation acceptance gaps and known build-file locks. Recheck them using
a fresh unlocked build; do not treat historical green reports as current.

## Release policy

1. Keep existing uncommitted work intact; no blind `git reset`,
   `git clean`, replacement of production `dist` or undocumented push.
2. Establish a repeatable, single canonical CMake build
   (`cmake --preset release`, `cmake --build --preset release`).
   Close any running editor/player processes holding output DLLs before
   re-linking, without discarding unsaved project edits.
3. All automated gates must pass against that current source revision.
   Failures may not be solved by deleting tests or relaxing assertions.
4. Every manual acceptance item needs dated evidence (capture, log,
   reproducible steps, environment, source revision, build hash).
5. A clean-machine release package, licensing audit and signed evidence
   are mandatory before using the label NorthStarCpp 1.0.

## Milestone 1 — gameplay foundation (P0)

**Goal:** one fully controlled and animated Pawn with reliable camera, physics,
input, events, AI interaction and save/load.

Engineering work:
- Diagnose Root Motion rejection at tick 1; verify ownership of the
  animated mesh, possession, fixed-timestep conservation and wall sweeps
  without bypassing collision.
- Investigate skeletal viewport regression; preserve **last valid
  animated pose** across turning, transient clip residency and blend
  transitions; never substitute bind pose in a visible frame.
- Test PlayerController/Pawn/CharacterMotor/CameraRig lifecycle:
  spawn, possession, unpossess, teleport, vehicle attach/detach.
- Exercise jump/fall/landing events, one-shot animation notifies and
  correct V8 subscription cleanup.
- Validate save/load of actual Pawn state, AI and vehicle state, not just
  static Scene XML.

**Exit criterion:** live captured movement and animation tests, physics
collisions and deterministic save/load pass with no bind-pose flashes.

## Milestone 2 — editor production workflow (P0)

**Goal:** author a playable project without changing engine C++ source.

Engineering work:
- Reconcile canonical weapon-prefab data with the strict prefab-contract
  test (test exit 31); identify intentional mass/scale/attachment changes.
- Fix `prefab_composition` and `project_script_assets` regressions with
  source/asset compatibility maintained.
- Verify Qt native editor: Details search/categories, Outliner selection,
  atomic Undo, drag/drop, material edit, prefab edit and Play-in-Editor.
- Verify authoring a scene with multiple NPCs, scripts inside project
  assets, collisions, animation graph and camera controls.
- Validate project export and error diagnostics via editor UI.

**Exit criterion:** record screen capture of create -> import -> configure
-> simulate -> stop -> export -> launch workflow on one project.

## Milestone 3 — vertical slice (P1)

**Goal:** 15–30 minutes of complete KaylaNightCity gameplay.

Deliverables: one physically navigable district; player, multiple animated
NPCs with autonomous schedules/pathfinding and obstacle avoidance; working
transport with synchronized passengers; one interaction/mission with
start/finish/checkpoint; UI and audio.

**Exit criterion:** full uninterrupted gameplay capture plus built-game
verification from the exported package. Current FPS/vehicle/scripting
unit tests alone are insufficient.

## Milestone 4 — production stability (P1)

Profile **ECS -> Physics -> Animation -> Render Graph -> GPU** in the exact
vertical-slice scene. Collect CPU and GPU frame p50/p95/p99, 1% low,
peak VRAM/RAM, resource load traffic, stalls, crash reports and
long-running stability. Stabilize D3D12 first, then validate Vulkan
parity with identical cameras, weather, lights and post-processing.

**Exit criterion:** agreed target hardware, budgets, reproducible traces,
and tracked performance regressions. No invented FPS uplift claims.

## Milestone 5 — engine 1.0 (P1 release gate)

Create a clean installable engine + Qt editor + SDK + standalone player,
from a reproducible build. Verify third-party licenses, optional plugin
replacements, shader/asset packaging, error logs, empty-project
creation and a game built independently of developer toolchains.

Use existing `tools/build_dist.py` and
`tools/release_readiness_gate.py` for authoritative package/security
checks. Do not substitute the milestone CTest subset for those gates.
Release evidence must be collected on an independent clean Windows VM.

**Exit criterion:** reproducible fresh build and installed package,
documented compatible dependencies, test and runtime evidence, and
a sign-off that explicitly covers known limitations.

## Automated milestone smoke gate

`tools/release_milestone_gate.py` supplies a non-destructive CTest
coverage map and machine-readable evidence. Its design intentionally
marks manual acceptance **pending** even after all tests pass.

```powershell
# Discovery only, does not build or execute tests:
python tools/release_milestone_gate.py

# Run one focused acceptance subset, preserve an immutable JSON report:
python tools/release_milestone_gate.py --stage 1 --run
python tools/release_milestone_gate.py --stage 2 --run

# All CTest subsets (not equivalent to full regression or release acceptance):
python tools/release_milestone_gate.py --stage all --run
```

Default evidence: unique timestamped
`out/qa/milestone_<stage>_<UTC>.json`, plus corresponding JUnit files.
An existing evidence filename cannot be overwritten. The JSON includes
source dirty-state, registered tests, missing coverage, individual
test results and manual acceptance requirements.

**Important:** CTest on an old binary is **not proof** of current source
correctness. Rebuild the relevant targets and rerun this gate before
updating any milestone status.

## Immediate engineering queue

| Priority | Work item | Proof required |
|---|---|---|
| P0 | Root Motion tick-1 rejection | physics regression + 80-tick collision trace |
| P0 | Skeletal viewport pose continuity | automated rig/transition checks + live GPU capture |
| P0 | Canonical weapon prefab mismatch (exit 31) | validated serialization and authored mass/scale contract |
| P0 | Prefab Composition / Project Script Assets | targeted tests + actual Qt asset workflow |
| P0 | Locked/stale build and V8 Player startup consistency | clean build + D3D12/Vulkan boot reports |
| P1 | NPC pathfinding/animation/interaction in KaylaNightCity | 15–30 minute playable slice |
| P1 | p95/p99, memory and Vulkan/D3D12 parity | comparable traces and captures |
| P1 | Reproducible engine package and clean-machine QA | signed release evidence |

No claim is made that stages 1–5 have been completed in this audit.
