# Prefab Authoring — NorthStarCpp Scene V3

Copyright (C) 2026 Kayla's Systems — Kayla Verner.
Status: P0 foundation; editor integration source-compiled, GUI acceptance pending.

## Purpose

The editor captures a selected entity and its descendants as a reusable
project-local .nprefab asset (stored in the NSCN binary format). The prefab is a validated Scene V3 fragment.
Instances become ordinary Scene V3 entities and use normal ECS, physics,
streaming, scripting and game distribution contracts.

## Editor workflow

1. Save a .nscn binary project scene and select a hierarchy entity.
2. Assets > Export Selection as Prefab... (writes prefabs/*.nprefab).
3. Assets > Instantiate Prefab... (loads a project-local .nprefab).
4. With a selection, the prefab root becomes its child and is placed
   beside it on X. With no selection, it becomes a root at the origin.
5. Insertion uses the editor's existing commit_scene transaction and
   therefore participates in Undo/Redo and regular scene Save.

The Content Browser shows .nprefab under scene assets.
Script paths and mesh/material asset references are VFS-relative pointers,
not embedded resources. Dependencies must be present in the destination project.

## Native API

- editor_prefab.hpp: capture_prefab(Scene const&, size_t root_index)
- editor_prefab.hpp: instantiate_prefab(Scene&, Scene const&, Vector3 anchor,
  uint64_t parent_id = 0) -> size_t (new root index)
- Implementation: modules/editor/editor_prefab.cpp
- Regression target: northstarcpp_editor_prefab

## Invariants

- One root per template; only the selected subtree is captured.
- Object positions and prefab streaming cells are root-relative.
- New stable IDs are assigned on each instantiation; all parents remapped.
- Imported streaming cell names are made unique per instance.
- Layers are merged by name; existing destination visibility prevails.
- Player-controller flags are cleared to avoid duplicating players.
- Invalid parents, references, transforms and bounds fail transactionally.
- The existing 16,384-object cap and Scene V3 validation still apply.
- Scene-global triggers and portals are not captured: they have no
  entity ownership relation in the current Scene V3 contract.
- Prefab export/import restrict file paths to the project's prefabs folder.

## Verification — 2026-11-14

- Fresh CMake Release build of editor_prefab_smoke: PASS.
- File save_scene/load_scene roundtrip using a .nprefab file: PASS.
- CTest northstarcpp_editor_prefab: PASS 1/1.
- CTest northstarcpp_save_game: PASS 1/1.
- Both targeted CTests in one run: PASS 2/2.
- GCC C++17 window.cpp syntax checks as Editor and Player: both PASS.
- Existing editor_authoring build: dependency compilation timed out.
- Full Editor/Player re-link, GUI click acceptance and standalone package:
  NOT YET VERIFIED. Installed binaries unchanged.

## Known limitations

This is a baked-instance prefab system, not live asset inheritance.
Future work: prefab asset GUID, nested prefab references, per-instance
overrides and apply/revert, dependency graph, visual placement, integration
tests on shipped Player, and asset migration across projects.
