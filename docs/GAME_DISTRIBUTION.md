# NorthStarCpp — Standalone Game Distribution

**NorthStarCpp 0.7.0:** pass the separate verified `GameClient/` runtime as `--engine-package` to `tools.build_game`. A unified pre-0.7.0 distribution is still accepted for compatibility. See [ENGINE_CLIENT_SPLIT_0_7_0.md](ENGINE_CLIENT_SPLIT_0_7_0.md).

## Build and verify the engine

First build and verify the complete Runtime, SDK, and registered module set:

```powershell
.\northstarcpp.cmd dist --build-type Release
```

## Export an individual game

The source project must contain `northstarcpp.project.json` and a binary startup scene named `main.nscn` (legacy `.nscene` accepted for existing projects).

```powershell
.\northstarcpp.cmd game-dist --project Projects/test --engine-package dist/NorthStarCpp-0.2.0-windows-x64-Release --physics bullet
```

Use `--physics jolt` to select Jolt. For development exports, `--allow-debug` can be specified. Without that flag, verified Release engine binaries are required.

Output: `dist/games/<game>-<engine-version>-windows-x64-Release-bullet/` and a ZIP archive. The package includes `launch.cmd`, `bin/northstarcpp_player.exe`, `bin/bugtrap.exe`, one DLL for each active capability, `game/main.nscene`, `game/assets`, licenses, and a SHA-256 `manifest.json`.

Verify an exported game:

```powershell
.\northstarcpp.cmd game-dist --verify dist/games/<game>-<engine-version>-windows-x64-Release
```

On Windows, start the game using `launch.cmd`. The exporter automatically tests the installed player in `--check-scene` mode: scene loading, world initialization, and frame rendering.

## Version 0.2.0 limitations

- New exports run `.nscn` binary scenes (legacy text scene packaging remains compatible); the exporter does not compile arbitrary custom C++ gameplay code.
- The `assets` directory is copied, but the asset import, cooking, and streaming pipeline is not yet complete.
- `scripts` are not included as executable gameplay because the current player does not execute them.
- Third-party engine code and game assets require separate redistribution-rights review.

See [ENGINE_MIGRATION_STATUS.md](ENGINE_MIGRATION_STATUS.md) for the current feature status.

Package names include the selected physics provider (`-bullet` or `-jolt`), so both variants can be released without path conflicts.
