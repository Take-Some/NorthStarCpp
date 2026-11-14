# NorthStarCpp

**Project author and maintainer:** Kayla Verner  
**Current development year:** 2026

NorthStarCpp is a C++ engine and development toolkit built with **GNU GCC / MinGW-w64**, **CMake**, and **Ninja**.

## Getting started

Double-click **`northstarcpp.cmd`** to start the native **C++/Qt 6 Widgets Project Browser**. It uses the light theme, Font Awesome 6 Free icons, and the reusable C++ project catalog. No Python UI is launched. See [Qt Project Browser](docs/PROJECT_BROWSER_QT.md). If the native executable is missing, the script builds it using GNU GCC.

From a terminal in the repository root:

```powershell
.\northstarcpp.cmd doctor
.\northstarcpp.cmd configure
.\northstarcpp.cmd build
.\northstarcpp.cmd test
.\northstarcpp.cmd editor
.\northstarcpp.cmd run --target player
```

## Features

- Bullet 3.25 rigid-body physics in the editor and player.
- Create and manage projects in the native NorthStarCpp Project Manager.
- Build the `northstarcpp_math` library and `northstarcpp_runtime`.
- GCC targets for `northstarcpp_editor`, `northstarcpp_player`, `shader_generator`, and regression tests.
- Load and save sample scenes in the versioned binary `.nscn` format (legacy `.nscene` imports are supported).
- Scene V3 prefab authoring: export a hierarchy into project-local .nprefab, instantiate with remapped stable IDs and transformed streaming cells (editor, asset browser and undo/redo). See [Prefab authoring](docs/PREFABS.md) and [NSCN binary scenes](docs/BINARY_SCENES.md).
- Project-owned game saves through native SaveGameStore and allowlisted JavaScript engine.save methods (bounded JSON slots, atomic replacement, integrity checks). See [Game saving](docs/SAVE_GAME.md).
- Replaceable DLL providers for ECS, physics, audio, input (`engine.input`), rendering, and assets. Physics providers are discovered in the module directory. Both Bullet 3.25 and Jolt 5.6.0 are included. Other built-in providers include Flecs ECS 4.2.0, WinMM, and Direct3D 11. See [Module loading](docs/MODULES.md).
- An authoritative ECS world for editor objects through API v2, with persistent IDs and replaceable ECS providers. See [ECS architecture](docs/FLECS_ECS.md).
- An `engine.input` plugin supporting keyboard, mouse, Unicode text, and up to four XInput controllers, including axes, buttons, and vibration. XInput is loaded dynamically. The plugin is built with the engine, installed through CMake, and included in `dist`.
- Provider-neutral `engine.scripting` API with persistent JavaScript contexts and a native V8 provider when a compatible V8 SDK is configured. See [Scripting API](docs/SCRIPTING_API.md).
- CTest coverage for ECS lifecycle, physics objects, input, and editor behavior.

- Native C++ `bugtrap.exe` built with editor/player/Project Manager; local crash reports and Windows minidumps. See [Crash reporting](docs/CRASH_REPORTING.md).
 `game-dist` validates an engine package, copies selected DLLs, the scene and assets, and creates `launch.cmd` and a ZIP archive with checksums. See [Game distribution](docs/GAME_DISTRIBUTION.md).
- `--check-scene` validates arbitrary scenes in the player without requiring a specific physics test scene.
- The first standalone FPS prototype, `Projects/NorthStarFPSArena`, has a 441-tile platform, nine targets, first-person view, mouse/XInput controls, and hit counting. Run the player with `--fps --scene Projects/NorthStarFPSArena/main.nscene`.

## Source layout

Active modules are organized by role and provider under `engine/northstarcpp/modules/`.
Imported dependencies reside under `engine/northstarcpp/modules/foundation/northstarcpp/compat/`; unused historical files are archived under `.northstarcpp/archive/`. CMake rejects plugins registered outside the module directory.

See [Source layout and extension rules](docs/ENGINE_LAYOUT.md).

## Current status

The native editor and player are early working implementations, **not a finished production game engine**. Asset processing, general gameplay systems, deeper subsystem integration, and full release validation are still in progress.

Internal C++ names and paths that remain necessary for compatibility are preserved. Original author attributions and license notices are retained.

Further reading: [Quick start](docs/START_HERE.md) · [GNU GCC shell](docs/GCC_DEVELOPMENT_SHELL.md) · [Authorship and licenses](docs/AUTHORSHIP.md).

## Distributable build

Run `.\tools\build_release.cmd` to build the current sources in Release mode and publish the verified engine, plugins, and SDK as a folder and ZIP archive under `distributable/`.

Use `northstarcpp.cmd dist` for configurable packages under `dist/`. See [Distribution and source architecture](docs/DISTRIBUTION.md).
