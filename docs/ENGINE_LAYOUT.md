# NorthStarCpp — Source Layout

**Reorganized:** November 13, 2026  
**Rule:** `engine/northstarcpp/` contains only `CMakeLists.txt` and the unified `modules/` directory. Vendored sources and archives belong elsewhere.

## Repository root

| Path | Purpose |
| --- | --- |
| `northstarcpp.cmd` | Opens the native Win32/C++ Project Manager without arguments; accepts CLI commands with arguments |
| `CMakeLists.txt` | Main CMake entry point |
| `engine/northstarcpp/modules/` | Host, editor, and provider sources |
| `include/northstarcpp/` | Public C/C++ headers |
| `external/` | Third-party dependencies and compatibility layer |
| `tools/` | Python utilities; launcher in `tools/dev/` and CMake helpers in `tools/cmake/` |
| `config/`, `docs/`, `examples/`, `tests/` | Configuration, documentation, samples, and tests |
| `Projects/` | User projects |
| `out/`, `distributable/`, `dist/` | Build outputs and distributable packages |

## Module tree

```text
engine/
└── northstarcpp/
    ├── CMakeLists.txt                # Module build and registration
    └── modules/
        ├── host/                     # Core, loader, VFS/scenes, subsystem host
        ├── editor/                   # Editor and player entry point
        ├── viewport/                 # Scene viewing and rendering integration
        ├── shared/                   # Shared provider implementation code
        ├── ecs/flecs/                # Flecs through ECS API v2
        ├── physics/bullet/           # Bullet Physics
        ├── physics/jolt/             # Jolt Physics
        ├── audio/winmm/              # WinMM audio
        ├── render/d3d11/            # Direct3D 11
        └── input/win32/             # Win32/XInput
```

The public C/C++ SDK stays in `include/northstarcpp/` because these files define **public contracts**, not specific provider implementations. The host depends on capability APIs, not on concrete Flecs, Bullet, or WinMM implementations.

## Third-party dependencies

```text
external/
├── flecs/                  # Flecs ECS
├── northstarcpp/compat/    # Compatibility layer
└── sdk/                    # Bullet, Jolt, and other SDK dependencies
    ├── include/
    ├── libraries/
    └── sources/
```

The compatibility layer was **relocated, not deleted**. CMake still builds math sources from `engine/northstarcpp/modules/foundation/northstarcpp/compat/core/sources` and installs the required SDK headers under the public `northstarcpp/compat/...` include paths.

## Archived files

Older IDE files (`NorthStarCpp.Source.sln/.suo`) and unused ECS duplicates are archived under `.northstarcpp/archive/engine-layout-2026-11-13/`. Previous configurations are in `.northstarcpp/backups/engine-layout-*/`; the move log is `.northstarcpp/migrations/engine-layout-v1.json`.

`tools/organize_engine_layout.py --verify` checks moved paths and archive checksums. It does not modify third-party library sources.

## Rules for new modules

Put new implementations only under `engine/northstarcpp/modules/<capability>/<provider>/`. Register them through `northstarcpp_module(...)` and `tools/cmake/NorthStarModules.cmake`, rather than hardcoding DLL paths in the host.

Built DLLs go automatically to `out/gcc-debug/modules/<capability>/`; installed DLLs go to `bin/modules/<capability>/`.

Do not duplicate plugins beside `CMakeLists.txt`, put project-owned modules under `external/`, edit vendor sources merely to rename them, or treat `Scene::objects` as the editor's primary state source instead of ECS.

## Verification

```powershell
.\northstarcpp.cmd doctor
.\northstarcpp.cmd configure
.\northstarcpp.cmd build
.\northstarcpp.cmd test
python tools/organize_engine_layout.py --verify
```

`tests/engine_layout_smoke.cmake` validates the module layout. CMake also validates the directory of every newly registered provider.
