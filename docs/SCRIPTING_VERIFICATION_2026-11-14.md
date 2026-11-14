# engine.scripting — Integrated Player and V8 Verification

Date: 14 November 2026
Workspace: `C:\Users\AidenFox\Documents\xray2`
Status: **Development integration verified; production release blocked by V8 SDK security/CRT packaging.**

## Current results

| Acceptance case | Evidence | Status |
| --- | --- | --- |
| Native `engine.scripting` ABI 2 | `northstarcpp_scripting_real_v8` | PASS |
| UCRT64 V8 14.0.210 in-process provider | `out/scripting-v8-ucrt-build/bin/scripting_v8.dll` | PASS (development only) |
| Full C++ player compilation/linking | `out/scripting-player-integration-20261114/northstarcpp_player.exe` | PASS |
| Real authored scene-file JavaScript | `scripting-e2e-results.json`, mode `source` | PASS |
| Real FAT/DB `--archive` JavaScript | `scripting-e2e-results.json`, mode `archive` | PASS |
| Lifecycle `start/update/stop` | Three `engine.save` markers per run, four update frames | PASS |
| Player exit codes | 0 in both runs | PASS |
| Python distribution/security regressions | 18 tests | PASS |
| Native V8 CTest | 1/1 | PASS |
| Vulnerability-reviewed/updated V8 SDK | No compatible patched UCRT64 artifact available in the verified MSYS2 repository | BLOCKED |
| Verified full signed distribution package | Not produced | BLOCKED |

## Player build and link repairs

The first complete Ninja build uncovered a zero-length (8-byte) `libBulletCollision.a`
left by interrupted compilation. Rebuilt that archive from compiled objects,
restoring BulletCollision exports.

The player then exposed an unresolved `EditorContentBrowser::~EditorContentBrowser()`.
`engine/northstarcpp/CMakeLists.txt` was corrected to compile
`modules/editor/content_browser.cpp` into the player and link `comctl32`.
After CMake regeneration, executed the exact Ninja-generated compilation and
link commands for the affected player object and executable. Both completed
with exit code 0. The resulting EXE runs actual Windows D3D11 WARP games.

Player SHA-256:
`ed8c5dc4110309926eab21044e05d77d9d2dadec6aafc288d1981b112c38cd46`

Note: This proves native compilation and linking. A completely clean,
single-invocation, all-target Ninja build is not yet recorded as passing,
because earlier Ninja termination damaged dependency/log metadata. Do not
treat this development EXE as a signed package.

## End-to-end gameplay verification

The QA folder `out/scripting-player-integration-20261114/ScriptedIntegrationQA`
is an isolated copy of the original 40-object
`Projects/NorthStarFirstPersonPlatform` scene and its JavaScript gameplay
logic, augmented with integration event handlers. The original project is
unchanged.

`tools/v8_provider/player_e2e.py` launched the actual
`northstarcpp_player.exe` using `--warp --fps --scene` and using
`--warp --fps --archive ... --scene level/main.nscene`.
`tools/v8_provider/archive_fixture.py` generated the real scene's archive.

Both runs verified:

- `start` callback, `engine.scene.objectCount()` returning 40;
- `engine.save.write()` writes a real, project-scoped save marker;
- four `update` callbacks with finite frame delta;
- `engine.session.quit()` exits player without external termination;
- `stop` callback writes the final marker;
- player exit code 0, with no stderr diagnostics.

Machine-readable evidence:
`out/scripting-player-integration-20261114/scripting-e2e-results.json`.

This confirms both source-project and **full player** archive startup; the
independent `archive_scripting_smoke` also passed for direct VFS/V8 hosting.

## V8 SDK and release safety

The checked MSYS2 UCRT64 V8 package is still `14.6.202.26-5`,
whereas CVE-2026-85046 is fixed beginning with V8 `15.3.48`.
The local development V8 SDK is `14.0.210`; it is not production-safe.
Do not deploy a vulnerable build to users.

The release pipeline in `tools/v8_provider/registry.py` and
`tools/build_dist.py` has an opt-in, version-gated V8 registration,
SHA-256 runtime manifest, conflict detection and third-party notices.
`tools/build_game.py` copies transitive DLL dependencies and game licenses.
A full installed/signed package cannot pass until a compatible, patched V8
SDK is built, its runtime dependencies and security reviewed, and the
distribution+game smoke checks succeed.

The native V8 DLL is pinned for the process lifetime; changing the provider
requires restarting the game/editor. No publishing, production DLL
replacement or `dist` overwrite was performed.

## Reproduction

```powershell
ctest --test-dir out/scripting-v8-ucrt-build -R northstarcpp_scripting_real_v8 --output-on-failure
python -m unittest tests.test_game_dist tests.test_v8_distribution -v
python tools/v8_provider/player_e2e.py `
    --player out/scripting-player-integration-20261114/northstarcpp_player.exe `
    --provider out/scripting-v8-ucrt-build/bin/scripting_v8.dll `
    --project out/scripting-player-integration-20261114/ScriptedIntegrationQA `
    --archive out/scripting-player-integration-20261114/qa-real-project-archive.db
```

The test V8 SDK may only be run against local trusted project scripts in
this development environment. For production, replace it with a compatible
security-updated SDK, rebuild `scripting_v8.dll`, then run the entire release
gate and native launch tests again.
