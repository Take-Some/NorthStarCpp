# NorthStarCpp — Scriptable World API Roadmap

Date: 2026-11-14

## Current working features

Shared Scene class exposes objectCount(), get(index), objects(start,count),
iterate(pageSize), findPlayer(), findByName(name), nearby(center,radius,maxResults),
moveStatic(index,position), and setColor(index,rgb). This is a script facade
over current engine.scene.* host calls and does not add new native entities.
The nearby() implementation currently scans paginated objects in JS, so it
should not be used every frame on a very large world.

## P0: Real runtime entity creation and deletion (not implemented)

Intended project API:

    import { Scene, Events } from "@northstar/engine";
    Events.on("start", () => {
        const crate = Scene.spawn({
            name: "Supply Crate",
            asset: "models/props/crate.ydd",
            position: { x: 10, y: 0, z: 3 },
            layer: "Gameplay",
            cell: "district/sector-04",
            collision: true
        });
        crate.setPosition({ x: 11, y: 0, z: 3 });
        // crate.destroy();
    });

Acceptance requires versioned provider-neutral native API support across:
- Scene and ECS: stable runtime IDs, generation-safe handles, create/destroy.
- Physics: incremental body add/remove for Bullet/Jolt, without rebuilding
  the world or resetting player and traffic.
- Renderer: mesh/material resolution through VFS, transient scene instance
  submission and deterministic disposal.
- World Partition: cell ownership, residency, prefetch, HLOD and collision.
- Lifecycle: scripts start/update/stop, hot reload rollback and scene restart.
- Validation: VFS allowlist, positions, memory budgets, resource ownership.
- Persistence: runtime save/restore must not overwrite authored scene files.

Do not fake this operation by appending render-only cubes and calling it spawn.

## P1: Prefabs, ECS components, and trigger volumes

- Prefab.instantiate("prefabs/roadblock.nprefab", transform)
- Entity.getComponent(), addComponent(), removeComponent(), onDestroy()
- Schema-based component inspectors powered by plugin component providers.
- Scene.findByTag(), Scene.queryByComponent() and native spatial queries.
- Triggers.onEnter(), onStay(), onExit() for authored SceneVolume regions.
- Physics.raycast(), overlap tests, contact callbacks, filter masks.
- Audio.playAt(), Effects.spawn(), UI.showPrompt() from project scripts.
- Timers/Tasks with automatic cleanup when the script instance stops.

## P2: Editor, procedural world, streaming, diagnostics

- Placement brushes, undo/redo, batching and prefab hierarchy in Qt Editor.
- WorldPartition.pinCell(), prefetch(), residency and HLOD diagnostics.
- Deterministic procedural placement with seeded noise and grid alignment.
- NavMesh obstacle updates and NPC route invalidation on object creation.
- Script profiler with per-handler CPU time, allocation and native-call counts.
- Asset streaming traces, script dependency graphs and package validation.

## Architectural invariants

- Gameplay definitions live in project VFS assets, not native hardcoded rules.
- Builtin engine.* providers must remain replaceable by external plugins.
- Named SDK imports stay stable, with IDE types generated where appropriate.
- Authoring the permanent scene is distinct from transient runtime spawning.
- No false claim of spawn/collision/persistence until integration tests pass.

## Verification for the next stage

- Create and remove 1,000 props repeatedly; zero leaked render/physics bodies.
- Collision tests in both Bullet and Jolt; fallback must fail explicitly.
- Fast travel, cell streaming, HLOD selection, script reload and restart.
- Save/restore source and FAT/DB archive projects; clean editor Play/Stop.
- CPU/GPU p50/p95/p99 frame time measured over at least 1,000 frames.
