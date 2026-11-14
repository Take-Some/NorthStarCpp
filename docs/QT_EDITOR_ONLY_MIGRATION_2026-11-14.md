# NorthStarCpp Qt Editor-only migration

Date: 2026-11-14
Supported Editor: northstarcpp_editor_qt.exe
Legacy editor removed from build: northstarcpp_editor.exe

## Code and build
- CMake no longer registers, builds or installs the Win32 legacy editor.
- northstarcpp_editor_qt.exe is the only supported Editor executable.
- Editor icon northstar-editor.ico is now compiled into the Qt Editor.
- window.cpp remains exclusively as a Player compilation unit. Removing
  it outright would break northstarcpp_player.exe. The old editor target
  and NORTHSTARCPP_EDITOR compile definition have been removed.
- Both Project Manager launch helpers require the Qt Editor, no fallback.

## Viewport and game launch
- Qt Editor supports RMB+WASD camera flight, Q/E vertical, Shift boost,
  Ctrl precision, RMB+wheel fly speed, MMB pan.
- Shaded proxy, Wireframe and Shaded+Wireframe, adjustable grid and speed.
  These are CPU geometry previews, NOT the final Vulkan/PBR renderer.
- Game > Play Game (F6) launches northstarcpp_player.exe using a saved
  project scene, passing --shared-assets for an existing assets folder.
- Stop Game (Shift+F6), Player executable selection, error/stdout
  reporting and persisted settings are included.
- Qt editor supports --smoke --smoke-output FILE and --player-check.

## Packaging and tests
- build_dist.py requires Qt Editor and excludes legacy executable.
- It validates the required Qt tool imports, rejects staged legacy exe,
  uses the Qt Editor for installed package scene authoring checks and
  Player for independent NSCN validation.
- Clean Windows acceptance and release gate now require Qt Editor.
- Old Win32-specific UI visual smoke retired in favor of Qt authoring.
- Added CTest northstarcpp_qt_editor_player_launch.

## Verified
- CMake configured and Ninja linked northstarcpp_editor_qt.exe with
  the native editor icon.
- Ninja target list includes Qt editor but not legacy editor exe.
- northstarcpp_qt_editor_authoring PASS.
- northstarcpp_editor_camera_picking PASS.
- northstarcpp_editor_player PASS: Qt Editor NSCN and Player validator.
- northstarcpp_logo_resources PASS: Qt editor icon is correct.
- Qt Editor --player-check PASS against an available Game Player.
- Python build_dist.py and release_readiness_gate.py syntax check PASS.

## Remaining release gates
- A full fresh Ninja build / full CTest suite is NOT yet verified.
  The QA build became locked while building Jolt/Flecs dependencies.
- Final installed clean-Windows package not yet checked.
- Production dist and existing deployed DLLs are unchanged.

## Later verification: full clean Release QA

A fresh independent Release/Ninja build completed all 711 operations,
the full CTest suite passed 89/89, and an installed 201-file QA package
passed Qt Editor, Player, Project Manager, Asset Browser smoke and manifest
checks. The staging directory contains northstarcpp_editor_qt.exe only;
northstarcpp_editor.exe is absent. Promotion is still blocked by the
unattested native V8 SDK and missing independently signed clean-Windows
VM acceptance report. See QT_EDITOR_ONLY_RELEASE_QA_2026-11-14.md.
