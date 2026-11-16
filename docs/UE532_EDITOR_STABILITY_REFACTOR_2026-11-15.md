# NorthStar Editor — UE 5.3.2 architectural study and stabilization (2026-11-15)

## Reference and licensing

Reference: https://github.com/Mccrae123/Unreal-Engine-5.3.2 (release branch,
observed commit `3aebb45780906b05ef46e94ebc0329ceef0dfd22`).

Reviewed source modules by reading the linked repository's raw files:
- `Engine/Source/Editor/LevelEditor/Private/LevelEditor.cpp`: shared
  `FUICommandList` with action execution and CanExecute gating.
- `Engine/Source/Editor/LevelEditor/Private/LevelEditorActions.cpp`:
  scoped edit transactions around editor operations.
- `Engine/Source/Editor/UnrealEd/Private/PlayLevel.cpp`:
  editor-session lifecycle with explicit requests and teardown.

Epic Games' Unreal Engine EULA governs these sources. This work implements
independent NorthStar abstractions and does not copy UE source.

## Implemented in NorthStar

1. `qt_pie_protocol.hpp`: one versioned IPC contract for cross-process
   Editor/Player messages, replacing repeated string literals.
2. `qt_pie_command_state.hpp`: pure, constexpr command-availability function
   for Play/Pause/Resume/Stop/Eject/Possess; UI no longer recomputes these gates.
3. `qt_editor_window_play.cpp`: centralized Qt chrome snapshot/hide/restore
   for PIE entry, Eject and Possess. On stopping an ejected session, the layout
   that the user edited during Eject is retained rather than overwritten.
4. `locateEngineSharedAssets`: shared search for engine assets from Player
   startup and the embedded WARP renderer smoke path. Prevents divergence in
   launch-vs-test behavior.
5. `tests/qt_pie_command_state_smoke.cpp`: compile-time checks plus runtime
   enumeration of all 32 possible UI state combinations.
6. `CMakeLists.txt`: independent `northstarcpp_pie_command_state_smoke`
   target and CTest registration.

The platform remains native Qt for the Editor and Win32 IPC for the Player.
No Unreal Engine binary or headers were added to the NorthStar SDK.

## Verification

- CMake configure `BUILD_TESTING=ON`: PASS.
- Isolated `northstarcpp_pie_command_state_smoke` compile/link: PASS.
- Smoke run: `NorthStar PIE command availability: PASS (32 state combinations)`.
- Direct C++ compilation of modified `qt_editor_window_play.cpp`: PASS.
- Direct C++ compilation of modified `qt_play_surface.cpp`: PASS.
- Direct C++ compilation of modified `window.cpp` (Player): PASS.

A fresh full editor build was **not** completed: the current repository's V8
sidecar staging attempts to download a V8 archive and fails with SSL CA status
77. Earlier build attempts of this shared worktree also encountered unrelated
physics-provider/vehicle ABI source errors. This is *not* a runtime QA PASS
for the newly refactored editor. The existing installed production EXE is
unchanged.

No FPS/frame-time or startup-time improvement was claimed without
corresponding measurements. No Git commit or push was made.

## Follow-up engineering targets (not implemented in this change)

- Centralize remaining menu/toolbar commands in a typed editor command
  registry with command-state queries, no duplicated shortcuts.
- Separate transactional scene edits from QWidget event handlers; one
  undoable command and no editor-to-engine side effects before commit.
- Introduce generation/cancellation tokens for async asset preview and
  thumbnail jobs to prevent stale callbacks after project switching.
- Profile editor startup, scene loading, thumbnail enumeration and GPU
  viewport separately (p50/p95/p99, memory, GPU timestamps), before
  making performance or stability claims.
- Run full GUI and WARP Play-in-Editor smoke after V8 staging/SDK
  dependencies and concurrent physics-source changes build cleanly.
