# NorthStarCpp 0.6.1 — PLAYING FROM EDITOR native overlay

## Request
Show uppercase text `PLAYING FROM EDITOR` at the top-left of the game window whenever the game is running embedded in Qt Editor's Play In Editor (PIE) mode.

## Implementation
- `engine/northstarcpp/modules/editor/qt_play_surface.hpp`: added the editor-owned label.
- `engine/northstarcpp/modules/editor/qt_play_surface.cpp`: created a small, noninteractive native `QLabel` child window at logical coordinates (16,16), size 233 × 34. The label uses high-contrast uppercase text, a solid dark background, and a yellow accent border. Its native HWND is kept above the Player HWND in the same Qt editor surface, without global topmost flags. `WA_TransparentForMouseEvents`, `WS_EX_TRANSPARENT`, and `WS_EX_NOACTIVATE` prevent it taking player input/focus. It is shown only after the embedded Player HWND is attached, hidden on detach, and destroyed with the Play overlay.
- `engine/northstarcpp/modules/editor/qt_editor_window_play.cpp`: embedded IPC smoke now validates text, native HWND parent, visibility and input transparency. Neither the project UI document nor renderer shaders are modified.

## Verification
- `cmake --build out/build-0.6.1-release --target northstarcpp_editor_qt --parallel 4` — PASS.
- Build-tree `northstarcpp_editor_qt.exe --embedded-game-smoke`: PASS, `label=1`, native attached, unobstructed, escape and workspace restoration.
- Build-tree `--embedded-render-smoke`: PASS, native WARP renderer and Pause/Step/Resume/Eject/Possess.
- Installed candidate: `out/qa/NorthStarCpp-0.6.1-PIE-Label-QA`; CMake install + Qt isolated runtime deployment succeeded (345 files).
- Installed `bin/qt_tools/northstarcpp_editor_qt.exe --embedded-game-smoke`: PASS (`label=1`).
- Installed `--embedded-render-smoke`: PASS.
- Installed `bin/northstarcpp_editor_qt.exe`, private Qt editor and `bin/northstarcpp_player.exe`: all Windows FileVersion 0.6.1.0.
- `git diff --check`: PASS (only preexisting line-ending warnings).

## Scope
Only embedded PIE sessions show the banner. Standalone Player launches and project packaging do not show it. The user's active project scripts, UI rendering documents, and scene contents were not altered.

## Release state
QA-only build. The normal production distribution was not replaced; no commit, tag or push performed. Existing unrelated changes were preserved. No pixel-level visual screenshot was taken; HWND/visibility/IPC smoke covers the overlay contract.
