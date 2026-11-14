# NorthStarCpp crash reporting (Windows)

The C++ target `bugtrap` builds `bugtrap.exe` on every normal engine build. It is installed next to `northstarcpp_editor.exe`, `northstarcpp_player.exe`, and the native project manager in `bin/`; standalone game exports also include it.

The engine registers a Windows unhandled exception filter plus a C++ terminate handler at process startup. On a fatal error, it attempts to create a Windows minidump (`.dmp`) and a small text report (`.txt`) using DbgHelp; the standalone `bugtrap.exe` opens a crash dialog and optionally shows the local report folder.

Reports are saved under `%LOCALAPPDATA%\KaylasSystems\NorthStarCpp\Crashes` (falling back to the Windows temporary directory). Report files remain local. No network transfer occurs.

## Windows icon and FileInfo

\`bugtrap.exe\` embeds the dedicated \`icons/Bug.ico\` icon. The editor embeds icons/northstar-editor.ico; the player and Project Manager retain the original icons/logoStar.ico brand icon. The file
also contains Windows \`VERSIONINFO\` resource data from
\`engine/northstarcpp/resources/bugtrap_version.rc.in\`:

- FileDescription: NorthStarCpp BugTrap - Crash Reporter
- CompanyName: Kayla's Systems
- FileVersion and ProductVersion: the authoritative CMake project version, e.g. 0.2.0.0
- ProductName: NorthStarCpp Engine
- OriginalFilename: bugtrap.exe
- LegalCopyright: Copyright (C) 2026 Kayla's Systems - Kayla Verner

Both resources are embedded in the EXE, so no loose icon or metadata file
is required after installation. \`bugtrap.exe --self-test\` checks that its
icon and VERSIONINFO resources are present.

Manual smoke test:

```powershell
cmake --build out/gcc-fps-release --target bugtrap
.\out\gcc-fps-release\bugtrap.exe --self-test
ctest --test-dir out/gcc-fps-release -R northstarcpp_bugtrap_smoke --output-on-failure
```

A clean `northstarcpp.cmd build` includes the reporter automatically. The distribution builder fails if `bugtrap.exe` is absent; `game-dist` copies the installed executable into each standalone game's `bin` directory.

This reporter is a native NorthStarCpp implementation, not the historical third-party BugTrap GUI/DLL. The old compatibility implementation still references `BugTrap.dll` and is not wired into the current host; do not misidentify `bugtrap.exe` as a build of that legacy third-party executable.

Limitations: a process killed by the OS, a power loss, severe memory corruption, or a failure of DbgHelp may prevent dump creation. Crash handling cannot guarantee recovery. A crash dump may contain sensitive process memory and should be reviewed before sharing.
