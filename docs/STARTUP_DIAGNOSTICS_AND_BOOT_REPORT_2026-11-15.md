# NorthStarCpp — Startup Diagnostics and Boot Report

Date: 2026-11-15
Status: Player startup report compiled, linked and verified through real Win32/WARP and native engine.logger sinks. Qt Editor main syntax QA passed, but installed Qt Editor executable was not rebuilt.

## Purpose

Every engine startup now records an ordered timeline of phases with BEGIN / PASS / WARN / FAIL, elapsed offsets, duration_ms and an explicit READY, READY_WITH_WARNINGS or FAILED summary.
Reporting begins before selecting the engine.logger plugin and replays early events into the normal provider once it is available. The C++17 module is modules/shared/startup_report.hpp, without engine.logger ABI changes.

## Player instrumentation

- process.begin: host mode, PID, platform.
- arguments.parse, arguments.summary: scene, archive, FPS, WARP, smoke and packaged mode.
- logger.initialize, logger.provider: provider/version and report path.
- scene.load, scene.summary: scene version, object, component, cell and layer counts.
- script.discovery, script.mode, scripts.initialize, scripts.provider: game scripts and actual runtime provider.
- fps.assets: optional source configuration.
- physics.provider: physics runtime and version.
- streaming.zones, streaming.update_initial, scene.partition: streaming status, World Partition and VFS provider.
- camera.initialize: camera initialization.
- ecs.provider: ECS runtime and entity count.
- assets.provider, audio.provider: active/deferred VFS/audio providers.
- window.classes, window.create, window.surface, window.show: Win32 native creation.
- input.provider: input provider and version.
- editor.controls, window.layout: legacy Win32 editor-specific UI, where applicable.
- renderer.provider, renderer.asset_mounts, renderer.skydome: backend/version and initial GPU resources.
- frame.timer, frame.first: game frame loop scheduling and first fully executed frame.
- summary: total startup time and all diagnostic counts.

The current Qt Editor (northstarcpp_editor_qt) additionally records qt.application, qt.arguments, qt.window, scene.selected, scene.load and qt.window_show. Qt startup readiness denotes the UI is shown; it does not claim that a GPU frame is complete.

## File outputs

Plain log: NorthStarCpp-Player-<PID>.log
Structured log: NorthStarCpp-Player-<PID>.ulog
Machine report: NorthStarCpp-Player-<PID>-startup.json (schema northstar.startup.v1).
Default directory on Windows: %LOCALAPPDATA%\KaylasSystems\NorthStarCpp\Logs.
Qt Editor session is NorthStarCpp-QtEditor.
Using NORTHSTARCPP_LOG_DIRECTORY redirects all of the above to an explicit folder.
Normal logger output respects logging.level from bootstrap.json; INFO or lower is needed to see complete INFO phase entries. startup.json persists the full timeline independently of the logger's level filter.
In unattended smoke/validation modes, the process writes to stderr only unless NORTHSTARCPP_LOG_DIRECTORY is provided. Logger initialization failures fall back to stderr. User-profile logs are not created merely by CI smoke execution.

## Example

    $env:NORTHSTARCPP_LOG_DIRECTORY = 'C:\Temp\NorthStarCppLogs'
    .\out\native-skin-final-qa-20261115\northstarcpp_player.exe --warp --explore --scene .\Projects\NorthStarFirstPersonPlatform\main.nscn
    Remove-Item Env:\NORTHSTARCPP_LOG_DIRECTORY

To explicitly show the logger console, set logging.console.visible=true in bootstrap.json beside the executable.

## QA

- Pure C++17 test northstarcpp_startup_report: PASS. Verifies phase timing, warnings, exceptions, one-time replay and structured JSON.
- Win32 Player and Viewport object compilation: PASS in the current QA tree.
- Qt Editor main source with current Qt compiler flags: syntax check PASS.
- Real Player logger-file test tests/startup_report_runtime_smoke.py: PASS. Inspected genuine Player .log/.ulog/JSON for runtime initialization, Win32/renderer smoke and a failed pre-window scene load.
- Repeated combined CTest startup_report + startup_report_runtime: 2/2 PASS, 8.44 seconds.
- Interactive non-smoke Player, WARP software renderer: a persisted report with 62 events and READY status, 0 warnings/0 failures; frame.first PASS took 3267.871 ms, startup total 4191.409 ms. These times are software-rendered QA measurements, not GPU-frame performance.
- A manual QA Player link using actual CMake-generated compile/link commands was successful after a concurrent D3D12 Render Graph source-list fix and input provider linkage; standard all-target clean build and installed production distribution are not verified/replaced.

## Reliability and security

Missing individual optional assets stay warnings and should not abort startup. Broken required engine providers/ABI and invalid root scene remain fatal with a complete FAILED report.
No command-line credential dump, environment dump or arbitrary script source is included in startup metadata. Logs contain requested scene paths and provider identity.

No Git commit or push performed.
