# NorthStarCpp — GNU GCC Development Shell

NorthStarCpp uses MinGW-w64 / GNU GCC, CMake, and Ninja. Use `northstarcpp.cmd` or the native Win32/C++ Project Manager.

## Commands

```powershell
.\northstarcpp.cmd doctor
.\northstarcpp.cmd configure
.\northstarcpp.cmd build
.\northstarcpp.cmd test
.\northstarcpp.cmd editor
.\northstarcpp.cmd run --target player
.\northstarcpp.cmd projects
.\northstarcpp.cmd audit
.\northstarcpp.cmd logs
```

| Command | Purpose |
| --- | --- |
| `doctor` | Check CMake, Ninja, GCC, and engine sources |
| `status --json` | Machine-readable build status |
| `projects` | List user projects |
| `configure` | Configure the Ninja build |
| `build` | Compile available GCC targets |
| `test` | Build and run CTest |
| `editor [scene]` | Open the scene editor |
| `run --target player` | Start the native player |
| `audit` | Check source code for potential compiler incompatibilities |
| `logs` | Display recent build logs |

For Release builds, use `--build-type Release` with configure, build, and test commands.

## Main CMake targets

- `northstarcpp_math` — math library.
- `northstarcpp_runtime` — scene data and runtime logic.
- `northstarcpp_editor` — native editor.
- `northstarcpp_player` — native player.
- `shader_generator` — shader compiler utility.
- `northstarcpp_portability_smoke` — GCC compatibility test.

Executables and libraries are written to `out/gcc-<build-type>/`, and logs are stored under `.northstarcpp/logs/`.

## Limitations

The math library compiles imported internal engine sources. Some low-level headers, types, namespaces, and platform macros remain necessary for source compatibility.

Passing CTest confirms the modules actually covered by those tests, not the readiness of the full game engine.
