# NorthStarCpp 0.6.2 — Windows x64 Release and QA

Date: 2026-11-16. Source branch: main. Source HEAD at configuration: dfffc9d6. Existing uncommitted source changes were intentionally retained and included in this build; no commit, tag or push was performed.

## Version and compilation

- Updated `CMakeLists.txt` project version from 0.6.1 to **0.6.2** and the `README.md` version badge.
- Clean independent build directory: `out/build-0.6.2-release`.
- Toolchain: MinGW-w64 GCC 16.2.0 / Ninja, CMake Release, Windows x64.
- Configure: PASS. Windows VERSIONINFO attached to 187 EXE/DLL targets.
- Full build: PASS, 327 Ninja operations.
- Checked FileVersion/ProductVersion = **0.6.2.0** for `northstarcpp_editor_qt.exe`, `northstarcpp_player.exe`, `northstarcpp_asset_browser.exe`, `render_d3d12.dll`, `render_vulkan.dll` and `physics_jolt.dll`.

## CTest

- Total: 286; **260 PASS, 26 FAIL**, **91% PASS**.
- Full suite output: `out/qa_ctest_062.log` (191.84 seconds).
- Failing tests:
  `northstarcpp_game_distribution`,
  `northstarcpp_qa_reporter`,
  `northstarcpp_d3d12_pass_graph`,
  `northstarcpp_project_character_warp`,
  `northstarcpp_scene_obj_mesh`,
  `northstarcpp_scene_animation`,
  `northstarcpp_entity_event_batch`,
  `northstarcpp_project_gameplay_scripts`,
  `northstarcpp_bugtrap_crash_capture`,
  `northstarcpp_qt_menu_hover`,
  `northstarcpp_qt_text_asset`,
  `northstarcpp_prefab_browser`,
  `northstarcpp_nef8_modal_preview_qt`,
  `northstarcpp_logo_resources`,
  `northstarcpp_play_in_editor_render_qt`,
  `northstarcpp_shared_assets`,
  `northstarcpp_logger_application_lifecycle`,
  `northstarcpp_startup_report_runtime`,
  `northstarcpp_jolt_selected`,
  `northstarcpp_jolt_physics`,
  `northstarcpp_camera_streaming_physics_jolt`,
  `northstarcpp_scene_binary`,
  `northstarcpp_scene_nef8_crosslang`,
  `northstarcpp_ui_world_runtime`,
  `northstarcpp_player_pistol_runtime`,
  `northstarcpp_capability_swap`.
- These are not waived. One `northstarcpp_play_in_editor_render_qt` failed in the concurrent CTest run; other individually exercised PIE modes are detailed below.

## Independent installed QA candidate

- QA install: `out/qa/NorthStarCpp-0.6.2-windows-x64-Release`.
- CMake installation: PASS; Qt isolated-runtime deploy: PASS, four launchers.
- Installed tree: 346 files, 408,253,483 bytes.
- Installed private Qt editor `--help`: exit 0.
- Installed private Qt editor `--smoke`: PASS.
- Installed private Qt editor `--gpu-smoke`: PASS.
- Installed private Qt editor `--embedded-game-smoke`: PASS, `label=1` confirms `PLAYING FROM EDITOR` badge in native overlay, full client embedding, Escape and workspace restoration.
- Installed Player `--check-scene` for `Projects/AbbyPistolDemo/main.nscn`: exit 0, startup `READY`, no errors.
- Installed Player `--check-scene` for `Projects/KaylaNightCity/main.nscn`: exit 0, startup `READY`, no errors.
- These `--check-scene` results do not prove stable extended gameplay or full physics simulation.

## Release disposition

**QA candidate only.** The build and installed smoke checks pass, but the 26 failures block production promotion. Existing production binaries/dist were not replaced; no release publication, commit, tag or push. The source working tree contained unrelated uncommitted changes, preserved in place.
