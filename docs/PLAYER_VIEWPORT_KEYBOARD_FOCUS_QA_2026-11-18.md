# Input Focus / WASD Regression — NorthStarCpp

Date: 2026-11-18
Scope: game Player and Play-In-Editor (Qt parent + native child swapchain).

## Root cause

The Win32/XInput provider receives keyboard virtual-key updates through
`WM_KEYDOWN` and `WM_KEYUP` forwarded by the main Player window procedure,
and clears the stored keyboard state on `WM_KILLFOCUS`.
The GPU swapchain is a *child HWND*. Clicking a GPU UI element or the child
viewport can transfer keyboard focus to that child.

Before this fix, `App::viewport_message` forwarded neither child
`WM_SETFOCUS` nor child keyboard down/up events to the shared
`InputDevice`. The renderer could still receive relative mouse input
from `WM_INPUT`, while the game script saw `Keys.W/A/S/D` as false.

## Fix

Source: `engine/northstarcpp/modules/editor/window_runtime_win32_viewport.inc`.

- Forward child viewport `WM_SETFOCUS` and `WM_KILLFOCUS` to the same
  native Input Provider when the game is foreground.
- Forward child `WM_KEYDOWN`, `WM_KEYUP` (including system key
  variants) when gameplay is active.
- Give focused UI controls first opportunity to consume both key-down
  and key-up via `Viewport::ui_key`. Never apply gameplay input to
  an editable UI field.
- Do not route gameplay keys while the FPS menu, game script, PIE
  pause, or ejected mode owns the session.
- Do not use a separate `GetAsyncKeyState` game-only workaround;
  keyboard events stay in the replaceable `engine.input` provider.

The public multi-level API remains:
`engine.input.keyboard.isDown(Keys.W)` and the higher-level
`Input.isDown(Keys.W)` facade, which drives
`engine.player.motion.move(x,z)` via `PlayerMovementScript`.

## Evidence and QA

- Actual Player translation unit `modules/editor/window.cpp`
  (which includes the changed viewport code via
  `window_app_internal.hpp`) **compiled successfully** with
  Ninja Release configuration.
- `northstarcpp_input_smoke` built from source; **3/3 CTest PASS**:
  `northstarcpp_input_win32`,
  `northstarcpp_input_provider_independent`,
  `northstarcpp_input_reject_incompatible`.
- **6/6 Node tests PASS**:
  `abby_measured_movement_controls_smoke`,
  `input_enum_classes_smoke`,
  `shared_root_scripts_oop_smoke`,
  `player_action_script_api_smoke`,
  `player_look_ads_smoke`, `engine_api_nested_namespace_smoke`.
- Active projects (AbbyPistolDemo, KaylaNightCity,
  MovingCitiesPrototype, NorthStarFirstPersonPlatform):
  **12/12 JavaScript assets passed `node --check`**; zero active
  legacy flat `engine.input.*`, `engine.camera.*`,
  `engine.scene.*` direct calls detected.
- Original NEF8 projects' active `main.nscn` scenes contain Pawn
  components with the local auto-possess flag enabled. The default
  controller also supports this legacy scene authoring arrangement.

## Acceptance still required

Full `northstarcpp_player` link **PASS**: 579/579 Ninja build operations
completed with exit code 0 at 2026-11-18 17:10:52 (local system time).
The new `out/build/northstarcpp_player.exe` is newer than
`window_runtime_win32_viewport.inc` and the compiled `window.cpp.obj`.
The production `dist` directory was deliberately not overwritten.

End-to-end runtime after migrating stale test scripts to the hierarchical API:
- `northstarcpp_player_bundled_scripting`: PASS, including V8 startup,
  camera overrides, project config, save lifecycle and normal shutdown.
- `northstarcpp_player_scriptless_root`: PASS.
- `northstarcpp_player_asset_mount_regression`: PASS.
- `northstarcpp_player_scene_runtime`: FAIL, independently traced to
  `Native collider relocation failed` after moving a static scene entity.
  It is a Scene/Physics collider synchronization issue, not a keyboard
  event delivery assertion.
- Actual NorthStarFirstPersonPlatform and AbbyPistolDemo startup confirmed
  that their V8 game scripts execute; separate weapon attachment authoring
  warnings were observed and remain outside this focus fix.

Full Play-In-Editor and standalone visual keyboard/mouse testing
cannot be certified from unit tests alone.

Manual interactive QA:
1. Focus the standalone Player, press W/A/S/D, verify translation.
2. Jump with Space, sprint with Shift, and release keys; ensure
   movement stops immediately.
3. Switch camera mode with V and verify translation remains camera-relative.
4. In PIE, click game viewport; verify keyboard control after focus handoff.
5. Focus an editable UI element; typing W/A/S/D must not move Pawn.
6. Alt-tab away and back; verify no stuck key or lost keyboard input.

No commit, push or production-dist replacement performed.
