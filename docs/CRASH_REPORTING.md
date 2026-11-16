# NorthStarCpp BugTrap — Windows crash reporting

BugTrap is NorthStar's native out-of-process crash reporter, not the unrelated historical BugTrap.dll integration. `bugtrap.exe` has a dedicated Bug.ico icon and Windows VERSIONINFO resources.

## Runtime integration

The shared static component `northstarcpp_crash` registers its handlers at startup. Current entry points include:
- `northstarcpp_player.exe` (`modules/editor/window.cpp`)
- `northstarcpp_editor_qt.exe` (`modules/editor/qt_editor_main.cpp`)
- `northstarcpp_project_manager.exe` (`modules/project_manager/qt_project_browser.cpp`)
- `northstarcpp_asset_browser.exe` (`modules/editor/qt_asset_browser_main.cpp`)

The handler captures unhandled Windows SEH exceptions, `std::terminate()`, and CRT `SIGABRT` (including many assertion/fatal-abort paths). On installation it computes the local report directory and executable paths in advance. On fatal error it uses a reentrancy guard, writes a timestamped diagnostic report and a DbgHelp minidump, and starts `bugtrap.exe` as a **separate process**. The UI never depends on an event loop or renderer inside the crashed process.

Crash files are stored in:

```text
%LOCALAPPDATA%\KaylasSystems\NorthStarCpp\Crashes\
    crash-YYYYMMDD-HHMMSS-MMM-PID-TID.txt
    crash-YYYYMMDD-HHMMSS-MMM-PID-TID.dmp
```

If `LOCALAPPDATA` is unavailable, the crash folder is created below the Windows temporary path. The textual report records the UTC time, executable path, exception code and instruction address, PID/TID, minidump status and Win32 dump error. Reports remain local and are never uploaded automatically.

## Reporter UI

The native Win32 reporter is a self-contained dark-themed window with NorthStar yellow accents and:
- A visible fatal-error headline and exception code with a human-readable explanation for common errors such as `0xC0000005`, `0xC00000FD`, C++ terminate and SIGABRT
- A scrollable diagnostic report, which remains available even when minidump creation fails
- **Open report folder**, **Copy diagnostics** and **Close** actions
- The report path displayed at the bottom
- Dedicated application icon and version resources

The reporter only reads a local report supplied with `--report`; it never transmits data.

## Build and verification

Configure a Windows CMake build with `BUILD_TESTING=ON`, then:

```powershell
cmake --build out/editor-ue-parity-qa-20261115 --target northstarcpp_crash_capture_smoke
ctest --test-dir out/editor-ue-parity-qa-20261115 -R northstarcpp_bugtrap --output-on-failure
```

`northstarcpp_bugtrap_smoke` verifies the icon and version resources. `northstarcpp_bugtrap_crash_capture` starts disposable Windows child processes and validates a text report and **nonempty minidump** for each of:
1. Native SEH exception `0xE000B007`
2. `std::terminate()` / `0xE0000001`
3. `std::abort()` / `0xE0000002`

The test sets `NORTHSTARCPP_BUGTRAP_SILENT=1` so no window appears. `bugtrap.exe --report <path> --silent` validates a report file without opening the GUI. `bugtrap.exe --self-test` checks executable resources.

## Limitations

An explicitly killed process (`TerminateProcess`), power failure, kernel crash, security fail-fast that bypasses exception dispatch, overwritten unhandled-exception filters, or severe heap/stack corruption can prevent crash capture. The design reduces crash-path allocations and avoids the engine GUI, but the original process still invokes DbgHelp to create its dump; an external prestarted watchdog would be needed for stronger guarantees.

A dump can include user data from process memory. Review it before sharing. Manual verification of the actual window and DPI/theme rendering on a desktop remains necessary in addition to headless tests.

## Build integration status (2026-11-15)

`bugtrap.exe`, the crash fixture, Player, Qt Editor, Project Manager, standalone Asset Browser, and `physics_jolt.dll` all compile and link in `out/editor-ue-parity-qa-20261115`. The JoltPhysics string conversion in `jolt_mesh.hpp` was already corrected with `.c_str()`; rebuilding the plugin verified the fix. Shared `northstarcpp_qt_editor_viewport` now owns viewport/gizmo implementations and AUTOMOC for both the main Qt Editor and the standalone Content Browser, removing unresolved `QtEditorViewport` symbols without downgrading the prefab collision editor. An additional `QSpinBox` forward declaration, corrected `colliderShape_` member access and Qt signal-to-slot lambda were required to make the live editor sources compile.

Automated status: 2/2 BugTrap CTest checks pass (SEH, terminate and SIGABRT fixture scenarios); content browser + gizmo 2/2 pass; five Qt Editor/Project Manager integration smoke tests pass. Interactive GUI smoke: `python tests/bugtrap_gui_smoke.py out/editor-ue-parity-qa-20261115/bugtrap.exe out/editor-ue-parity-qa-20261115/bugtrap-ui-smoke.png`. It opens a synthetic local report, verifies the read-only text pane and three action buttons, exercises both 900x650 and minimum 740x530 layouts, captures real screenshots, clicks **Close** and verifies clean exit. Both screenshots at system DPI 96 were inspected and all actions are visibly labelled. A separate 125%-150% physical monitor DPI test remains unverified. The active Editor was observed running from the same build directory. Its in-memory old image is not replaced until it is restarted; do not terminate a live editor automatically.

The tested executables are available in the build output. No repository commit/push or external distribution release was performed.
