# NorthStarCpp — Historic Scene V2 Quick Start (Archived)

> Historical snapshot: contains discontinued Scene V1/V2 example and a removed
> first_scene.nscene. For supported commands use [Quick Start](../START_HERE.md).

## Canonical CMake build

Engine sources live in `engine/northstarcpp/modules/`. All CMake configurations
use one build tree: `out/build/`. Packages and logs live in `out/package/`
and `out/logs/`. To build: `cmake --preset release`, then
`cmake --build --preset release`. Do not run Debug and Release builds
concurrently. See [Single Build Layout](../SINGLE_BUILD_LAYOUT_2026-11-17.md).

## Graphical interface

Double-click `northstarcpp.cmd` to open the native Win32/C++ Project Manager. The former Tkinter project browser has been removed; Python is used only for CLI build tools and project data operations.

The native Project Manager provides project creation and opening. GCC builds, diagnostics, documentation and logs are accessible via CLI commands. Creating a project produces `Projects/<name>/assets`, `scripts`, and `northstarcpp.project.json`.

## Bullet 3.25 physics

Open `examples/first_scene.nscene` and select **Play**. The purple cube falls onto the floor. Press **W** to move the player toward the stationary block and **D** to push the green cube.

In the editor, **Mass (kg)** controls rigid-body behavior. A mass of 0 fixes the cube in place; a positive mass enables gravity and collisions. **Player-controlled** requires a positive mass. Select **Apply** after editing. **Esc** exits Play. Object positions reset when leaving simulation mode.

Version 2 of the `.nscene` format stores mass as the final number on each `cube` line. Version 1 scenes load with a default mass of 1 kg for the player and 0 kg for other objects. Newly saved scenes use version 2.

The C++ runtime exposes `northstarcpp::PhysicsScene` from `<northstarcpp/physics.hpp>`. Rigid-body simulation uses a fixed 120 Hz timestep. The build includes `BulletDynamics`, `BulletCollision`, and `LinearMath` sources under `sdk/vendor/sources/bullet-3.25`. The source tag is `3.25` at commit `2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5`. Its license is at `sdk/vendor/sources/bullet-3.25/LICENSE.txt`.

## Command line

```powershell
.\northstarcpp.cmd doctor
.\northstarcpp.cmd configure
.\northstarcpp.cmd build
.\northstarcpp.cmd test
.\northstarcpp.cmd editor
.\northstarcpp.cmd run --target player
```

The editor and player are early native applications; the complete game-development pipeline is still evolving. Physics providers are discovered automatically from the module directory. Other providers follow the [module configuration](../MODULES.md). The hub's separate **Launch Engine** control requires a configured `runtime` and the necessary game resources.

Example `config/northstarcpp.json` runtime configuration, to be used only when the game application is ready:

```json
{
  "runtime": {
    "executable": "out/gcc-debug/northstarcpp_player.exe",
    "working_directory": "out/gcc-debug",
    "required_paths": [],
    "args": []
  }
}
```

This is an example only. Do not enable it for an unfinished game without verifying resources and launch behavior.
