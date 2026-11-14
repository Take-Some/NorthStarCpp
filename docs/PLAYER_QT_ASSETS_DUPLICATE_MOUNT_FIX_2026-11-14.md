# NorthStarCpp — Qt Editor Play: Duplicate Asset Mount Fix

Date: 2026-11-14
Status: fixed in source, local Release build and installed QA package.

## Reported failure
On clicking Play in northstarcpp_editor_qt.exe, the game displayed:

    Cannot mount asset directory [engine.assets, .../assets_directory.dll]:
    Asset source already mounted

## Root cause
QtEditorWindow::playGame passed <project>/assets as --shared-assets.
The Player's scene v3 streaming path then mounted both <project>/assets
(the normal project mount) and the identical --shared-assets root
(the shared mount) into the same AssetStore with virtual prefix "".

The assets_directory provider intentionally rejects identical
prefix+canonical-directory mounts; this is not a DLL corruption problem.
The same flaw could also affect explicit asset-zone initialization.

## Fix
- qt_editor_window.cpp: Play Game now resolves only a genuine directory
  named sharedAssets, searching up from project and Player executable,
  never passing project/assets as shared-assets. No shared directory
  parameter is sent if none is available.
- window.cpp: same_asset_directory uses filesystem::equivalent with
  error_code to compare canonical existing directories including
  path aliases/junctions.
- setup_scene_asset_streaming: the project root takes precedence;
  a shared mount resolving to the same physical root is skipped.
- initialize_asset_zones: the same duplicate prevention applies to
  explicit and automatically discovered streaming-zone manifests.
- The provider itself still rejects accidental same-source duplicate
  mounts from unrelated callers. No blanket ignore/exception swallowing.
- Player --runtime-init-smoke exercises its full App constructor,
  including game physics, world streaming, zone asset mounting and
  optional project scripts, without creating a game window. The older
  --check-scene path skipped these steps and could not catch the issue.

## Regression tests
tests/player_asset_mount_smoke.py creates a genuine Qt Editor NSCN v3
scene, a project/assets root, and an empty valid zones.json.
It checks four runtime initialization configurations:
1. --shared-assets set to exact project/assets path.
2. --shared-assets set to equivalent project/assets/. path.
3. Explicit asset-zones with identical shared and project roots.
4. A distinct real engine sharedAssets directory.

All 4 scenarios pass and no error message occurs. Registered CTest:
northstarcpp_player_asset_mount_regression.

Final verified complete Ninja build: PASS (no work to do).
Full current CTest suite: 90/90 PASS.
Focused Play/Editor/scene tests: 5/5 PASS.

## Installed QA
out/asset-mount-playfix-qa-20261114/
NorthStarCpp-0.4.0-windows-x64-Release-QA

- 201 verified package files, 9 plugin records, manifest valid.
- The package includes new Qt Editor, Player, assets_directory.dll
  and qwindows.dll, and contains no legacy northstarcpp_editor.exe.
- Local isolated installed application smoke, Qt Editor --player-check
  and Project Manager smoke: PASS.

Release_approved remains false: the signed V8 security attestation and
independent clean Windows acceptance are still required. Production
dist and deployed DLLs were NOT replaced. This QA verifies runtime
initialization and process launch readiness; it is not a live manual
visual/gameplay walkthrough of the game window.
