# NorthStarCpp — Quick Start

Updated: 2026-11-18. This page describes the supported **Scene V3 / native Qt Editor / Player** workflow. Older Scene V1/V2 and discontinued examples are retained in the [historical quick start](history/START_HERE_SCENE_V2_ARCHIVE_2026-11-18.md), not presented as current.

## Prerequisites

Windows x64, MinGW-w64/GCC (with the same toolchain used for Qt 6 Widgets), CMake 3.21+, Ninja, Python and Git with initialized submodules. Some optional providers require additional SDKs, including native V8. See [GCC Development Shell](GCC_DEVELOPMENT_SHELL.md) and [Modules](MODULES.md) for dependencies and provider selection.

## Canonical build

From the repository root:

~~~powershell
git submodule update --init --recursive
cmake --preset release
cmake --build --preset release
ctest --test-dir out/build --output-on-failure
~~~

The debug, release and relwithdebinfo CMake presets all configure **the same canonical `out/build` tree**; do not run them concurrently. Other roots such as `out/q7` are isolated engineering QA trees, not the canonical release directory. Generated outputs belong under `out/`; production packages are managed separately under `dist/`. See [Single Build Layout](SINGLE_BUILD_LAYOUT_2026-11-17.md).

A fresh build can fail if running Editor or Player processes hold DLLs or Ninja metadata files. Report the blocker instead of overwriting a live production binary or deleting user edits.

## Launch and validate the tools

~~~powershell
.\northstarcpp.cmd doctor
.\northstarcpp.cmd configure
.\northstarcpp.cmd build
.\northstarcpp.cmd test
.\northstarcpp.cmd editor
.\northstarcpp.cmd run --target player
~~~

Running `northstarcpp.cmd` without arguments opens the native project-management workflow. Its build/test commands are convenience wrappers; CMake presets above are the underlying canonical build contract. The Qt Editor and Player require a successfully built runtime and compatible modules/assets.

## Work with a project

Projects are kept under `Projects/<project>/`, with a project configuration file, authored scenes, scripts, prefabs and asset roots. Real scenes in this repository include `Projects/AbbyPistolDemo/main.nscn` and `Projects/KaylaNightCity/main.nscn`. These are development/QA scenes, **not evidence that their complete gameplay is release-accepted**.

The current Player Framework is component-driven: a **Pawn Entity** has a separate controller, CharacterMotor, Camera Rig, and Skeletal Mesh/Animation composition. Movement uses a selected physics provider (Jolt is a current integration target); gameplay does not use a persistent `SceneObject::player` ownership flag. The selected View Target may differ from the possessed Pawn. See [Gameplay Production Gates](GAMEPLAY_FRAMEWORK_PRODUCTION_GATES_2026-11-18.md).

Scene V3 and World Partition: [Scene Streaming](SCENE_V3_STREAMING.md). Asset identity, NEF8 and shared resources: [Asset Pipeline](ASSET_PIPELINE_P1_FOUNDATION_2026-11-18.md) and [Prefabs](PREFABS.md). Editing and publishing: [Qt Project Browser](PROJECT_BROWSER_QT.md) and [Distribution](DISTRIBUTION.md).

## Interpreting test results

A successful single CTest run means the selected target passed on that build, not that every renderer or runtime mode is production-ready. See [Current Status](CURRENT_STATUS.md) for the latest documented acceptance boundaries and [Documentation Index](INDEX.md) for all subsystem manuals and dated QA notes.

## Historical compatibility

Bullet 3.25 and the older `.nscene` V1/V2 cube tutorial are part of repository history, but the referenced `examples/first_scene.nscene` file is no longer present. Do not use that old tutorial for the current Pawn, Jolt or Scene V3 APIs. Details and provenance remain in the [archived quick start](history/START_HERE_SCENE_V2_ARCHIVE_2026-11-18.md).
