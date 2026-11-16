# NorthStar — Player footsteps/V8 acceptance (2026-11-15)

## Implemented and verified

1. V8 runtime staging now supports an explicitly trusted local installation via
   `NORTHSTARCPP_V8_RUNTIME_DIR`. It validates `d8.exe --version` for 15.8.62
   and checks three files. The default official download still requires TLS
   verification and the pinned SHA256; it has not been weakened.
2. `northstarcpp_player` was successfully built in
   `out/editor-ue-parity-qa-20261115`, including the changed native
   `window.cpp`, audio provider and V8 sidecar. Ninja exited with code 0.
3. The Python Player E2E harness now distinguishes WARN lines for optional
   skipped invalid assets from real ERROR/FATAL runtime failures. Its
   bundled-script and scene-runtime E2E tests passed.
4. Player script discovery now treats a scene with an authored `player: true`
   as a scripted gameplay scene, even without `scripts/main.js`.
   Shared default `playerConfig.js` enables player services only when
   `Scene.findPlayer()` returns a player; a project override remains possible.
5. The default NAC playback bridge uses the configured `engine.audio` provider
   rather than hardcoding NorthStar Audio. ABI 2 supports overlapping voices
   and gain; ABI 1 falls back to single-stream playback.
6. The native NAC playback smoke opens the real
   `sharedAssets/audio/footsteps/wood.nac`, decodes audio, selects a
   `walk_*` clip, and plays/stops it through both NorthStar Audio and WinMM.
   Both CTest entries passed.
7. The real Player `northstarcpp_player_scriptless_root` E2E test passed
   without a project `scripts/main.js` or project player config. It
   verified native Player startup and V8 start/update/stop lifecycle.
8. Event/NAC/script tests passed: nine profile CTests, two Player E2E
   CTests, two real-NAC audio provider CTests and the scriptless-root test.
   Root event JS test also checks contract validation and automatic defaults.

## Build and verification commands

```powershell
cd C:\Users\Kayla\Documents\NorthStar\NorthStarCpp
$env:NORTHSTARCPP_V8_RUNTIME_DIR="C:\Users\Kayla\Documents\NorthStar\_qa_character_fix\scripting\v8"
cmake -S . -B out/editor-ue-parity-qa-20261115 -G Ninja
ninja -C out/editor-ue-parity-qa-20261115 -j 6 northstarcpp_player
ctest --test-dir out/editor-ue-parity-qa-20261115 -R 'northstarcpp_(root_events_and_footsteps|player_bundled_scripting|player_scene_runtime|player_scriptless_root|footsteps_nac_audio_)' --output-on-failure
```

The offline runtime directory is machine-specific and is not a bundled
artifact; remove the override to use the pinned-download path when TLS works.

## Open technical work

### Surface identity

`engine.player.ground()` queries the physics collision normal and scene
object index, then selects an authored surface tag such as
`surface:wood` on the collider layer or an inferred object name.
An untagged implicit floor defaults to `stone`.

**Jolt per-triangle physics material selection: IMPLEMENTED AND TESTED.**
An optional Physics Surface ABI v1 extension leaves ABI 4 unchanged and
resolves the original triangle index via Jolt MeshShape user data. NCM2
preserves material IDs through full scene serialization. The actual hit
material now takes priority in `engine.player.ground()`; older providers
fall back to authored collider-level tags or a stone default.
The `northstarcpp_jolt_triangle_materials` test passes for separate
grass/metal triangles and wood/snow triangles within one authored mesh.

Importing complete per-triangle collision material metadata from **all**
third-party YBN-like formats and full terrain production QA remain open.
Refer to `docs/PHYSICS_SURFACE_MATERIALS_JOLT_2026-11-15.md`.

### Audible game acceptance

**PASS — manually confirmed by the developer (2026-11-15):** footsteps
were audible during the game test on their system. This is user-reported
listening acceptance, not an instrumented device-level result. The
independent NEF8/NAC provider integration tests passed previously.
Quality checks for stereo perception, volume balance, stride timing and
rapid surface transitions remain separate. The additional native Player E2E
`tests/player_footsteps_runtime_smoke.py` has now **PASSED**, validating
Player -> physics contact -> player.footstep -> NEF8/NAC audio voice.
It has been registered as `northstarcpp_player_footsteps_runtime` in CTest.

### Delivery

Release distribution was not replaced; there was no Git commit or push.
The updated QA Player was successfully rebuilt in `out/collision-vhacd-qa` and used for native E2E verification. This QA build is not yet a production release artifact. A new build and copy into a controlled distribution location
are required to claim delivered binaries. CMake also warns about long
Windows object-file paths; use a short build root for future builds.
