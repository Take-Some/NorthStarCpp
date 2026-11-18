# NorthStarCpp — Runtime/Qt Editor decomposition, second phase

Date: 2026-11-16
Base revision at start: e6be4509
Build: _qa_editor_split / MinGW-w64 GCC 16.2.0, CMake, Ninja / Release

## Intent and scope

Preserve the Player simulation schedule, Win32 window callback behavior, project scene load, Qt dock/signal ownership and all engine.* built-in/external provider ABI tables. Do not copy Unreal Engine source or alter public plugin contracts. All source changes are local and uncommitted. Existing unrelated worktree modifications in native_model.hpp, host/mesh_asset_assets.cpp, viewport.cpp and viewport_skeletal_animation.cpp were not deliberately edited for this extraction.

## Real independent translation units

- qt_editor_window_inspector_workspace.cpp — inspector widget construction, collision authoring, content docks, provider inspector and workspace panels. QtEditorWindow::setupInspectorAndWorkspace() owns this initialization phase.
- qt_editor_window_workspace_signals.cpp — QtEditorWindow::wireWorkspaceSignals(): deferred signal/slot and commitInspector hookups, invoked immediately after panel creation with the original Apply button.
- qt_editor_window_outliner.cpp — QtEditorWindow::setupOutlinerPanel(): Outliner widgets and its owner-bound actions. A single startup-only callable is passed to preserve existing actor-menu parenting and available actions.
- window_win32_controls.hpp/.cpp — Windows UTF-8/wide-string conversion, widget text, numeric parsing/formatting. This is separately compiled into Player.
- window_runtime_frame_scheduler.hpp/.cpp — simulation dt clamping and PieFrameStepper decision. The caller still restarts FrameClock and constructs FrameProfileScope at the original points; scheduling is once per frame with no allocation.
- tests/runtime_frame_scheduler_smoke.cpp — runtime conditions for normal delta, 50 ms first-person clamp, PIE freeze, one 1/60 single-step and script pause reset.
- CMakeLists.txt and qt_editor_window.hpp — new source registration and Qt member declarations.

## Transitional App class member organization

Because App still owns substantial mutable private state, the following files are INCLUDE fragments of the same translation unit and NOT independently compiled C++ modules. The extraction preserved method bodies verbatim except for replacing the simulation step decision with the new function. This is an intermediate step before a Pimpl/state decomposition:

- window_runtime_bootstrap.inc — App construction/initialization.
- window_runtime_win32_viewport.inc — viewport message and static window procedure.
- window_runtime_win32_messages.inc — Win32 window command/event dispatch.
- window_runtime_scene_streaming.inc — scene streaming and asset-zone preparation/update.
- window_runtime_frame_loop.inc — entire frame pipeline, timing, scripts, inputs, animation, physics, rendering, streaming.

This approach keeps private object state and message routing in one class and avoids new lifetime aliases or extra frame loops. It improves source navigation; it does not reduce compiled code size or memory allocations by itself. For independent compilation of these methods, first introduce an explicit AppState/RuntimeServices (Pimpl) boundary and dependency lifetime tests.

## Source decomposition sizes

At phase start:
- window.cpp: 287009 bytes (~5708 lines).
- qt_editor_window_setup.cpp: 105505 bytes (~2146 lines).

After refactoring:
- window.cpp: 232580 bytes (4527 lines).
- qt_editor_window_setup.cpp: 43707 bytes (912 lines).
- New Qt inspector/workspace cpp: 33619 bytes (705 lines).
- New Qt workspace signals cpp: 30142 bytes (645 lines).
- New Qt outliner cpp: 11959 bytes (301 lines).
- New App .inc fragments: 216 + 209 + 172 + 278 + 286 lines.
- New Windows and frame helper modules: 59 and 13 lines in respective .cpp files.

These are source layout measurements only, not runtime performance gains.

## Build and acceptance

- Configured: cmake -S . -B _qa_editor_split -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON.
- Built: northstarcpp_player, northstarcpp_editor_qt, northstarcpp_runtime_frame_scheduler_smoke and the geometry override fixtures.
- Compilation/linking completed exit code 0.
- The shared build directory emitted Ninja 'premature end of file; recovering' warnings on some subsequent rebuilds. Investigate possible concurrent build or file modification before relying on it for final release reproducibility.
- Selected CTest run: 13/13 PASS, including Pie command-state, Runtime Frame Scheduler, Qt mouse-look, Qt Native UI, Editor authoring, Qt GPU surface, engine provider inspection, bundled V8 scripting, Player scene and prefab runtime, Qt Editor launching Player, scene validation, invalid-scene rejection.
- Standalone Pistol Runtime scenario FAILED with a 45-second timeout in this build. Prior audits already had failing Pistol Runtime scenarios, including missing collider; no baseline attribution or fix is claimed.

## Release conditions / further work

- Full clean and repeatable build on a non-shared build directory, with no Ninja recovery warnings.
- Exhaustive Qt panel lifecycle and exactly-once signal tests, repeated PIE Start/Pause/Step/Resume/Eject/Stop and resizing/focus transitions.
- Character+weapon collider integration and origin of the Pistol Runtime timeout.
- Extract App into independently compiled units via RuntimeServices, SceneBootstrap, Win32Host and FrameOrchestrator once private fields and resource ownership boundaries are made explicit.
- GPU frame p50/p95/p99, allocations/frame, CPU wall p95/p99 and leak/lifetime comparisons against a consistent pre-change baseline before asserting a performance win.
- No production dist replacement, no explicit version bump, no Git commit or push by this operation.

## Reproduction commands

```powershell
cmake -S . -B _qa_editor_split -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build _qa_editor_split --target northstarcpp_player northstarcpp_editor_qt northstarcpp_runtime_frame_scheduler_smoke --parallel 3
ctest --test-dir _qa_editor_split --output-on-failure -R '^northstarcpp_(runtime_frame_scheduler|pie_command_state|qt_editor_authoring|qt_editor_native_ui|qt_editor_gpu_surface|qt_editor_mouse_look|player_provider_inspection|player_scene_runtime|player_prefab_runtime|qt_editor_player_launch|player_bundled_scripting|player_scene_validation|player_reject_invalid_scene)$'
```
