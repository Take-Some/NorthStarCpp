# NorthStarCpp 0.4.0 — Release QA Build and FPS Test Project

Date: 2026-11-14
Repository: C:\Users\AidenFox\Documents\NorthStarCpp
Platform: Windows x64, MinGW-w64 Release
Status: QA verified, NOT APPROVED FOR PUBLIC DISTRIBUTION

## Build and verification

The full native CMake Release graph completed successfully after rebuilding
the current SceneWorld and SceneCellAsyncResidency streaming contracts.
The initial link failure was caused by stale objects during source changes;
reconfiguration/rebuild linked the current implementations without weakening
their contracts.

Full native CTest: 84 / 84 passed.
QA engine manifest verification: 200 files, 9 plugin providers, valid.
Installed native Player and Qt/editor packaging passed QA smoke.
Production dist was not overwritten or published.

QA engine directory:

    out\qa-fps-dist-20261114\NorthStarCpp-0.4.0-windows-x64-Release-QA

Engine Player:

    out\qa-fps-dist-20261114\NorthStarCpp-0.4.0-windows-x64-Release-QA\bin\northstarcpp_player.exe

## FPS Test Project

Directory:

    Projects\NorthStar_FPS_Test

Manifest: schema 2, name "NorthStar FPS Test", engine version 0.4.0,
game_mode=fps. Gameplay is native FpsArena and does not depend on V8.

Scene: main.nscn, NEF8 ListFile kind 6 schema 2, 14,097 bytes.
Scene contents: 677 objects, 1 player, 9 targets, 4 moving targets,
3 hazard zones, full test arena.
Player checks:
- Native --warp --check-scene --fps: exit 0.
- Native --warp --fps-ui-smoke --fps: exit 0.
- The installed QA engine also passed both checks.
- Native conversion of main.nscn to main.nscn is byte-for-byte stable.

Assets:
- gameplay/fps.json and gameplay/camera.json.
- audio/shot.wav and audio/reload.wav.
- Existing local sky assets from the First Person Platform sample.
- No JavaScript gameplay scripts are required.

Run from the project folder:

    run.cmd
    verify.cmd

## Standalone game QA packages

Under out\qa-fps-dist-20261114\games:

- NorthStar-FPS-Test-0.4.0-windows-x64-Release-bullet
- NorthStar-FPS-Test-0.4.0-windows-x64-Release-jolt

Both were created by tools/build_game.py against the verified QA engine,
not copied from a development build. Each includes 64 manifest-verified
files and passed the standalone --warp --check-scene smoke.
Launch a game by double-clicking launch.cmd inside the relevant folder.
No ZIP archives were generated.

## Source-tree fixture recovery

This checkout lacked historical regression resources still referenced by
CTest. Two test-only projects were created to restore executable coverage:

- Projects/NorthStarAnimationLab: a valid NEF8 V3 scene with an OBJ pedestal,
  animation JSON and minimal JS animation lifecycle. The OBJ, native scene
  animation and Node gameplay-script integration tests pass.
- Projects/NorthStarEngineQA: QA descriptor and real CTest inventory.
  The manifest groups 84 actual registered CTest names across 22 capability
  categories; it does not claim tests passed merely because they are listed.

These projects were reconstructed against the test contracts and are not
claimed to be recovered copies of missing original demo content.

## Outstanding release gates

The QA engine package reports release_approved=false. The existing
tools/build_dist.py contract refuses direct publishing to production dist.
Before authorized promotion, complete independent, signed clean-Windows
acceptance and applicable V8 provider/SDK security attestation.
A real interactive GPU session, long-run frame timings and player subjective
experience have not been demonstrated by the headless WARP smoke tests.

No production dist or original First Person Platform project was replaced.
