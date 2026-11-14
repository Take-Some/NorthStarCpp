# NorthStarCpp scripting release acceptance — 2026-11-14

**Current decision: RELEASE BLOCKED.** This document reports checked results,
not a claim that the engine is ready for public production distribution.

## Latest verification amendment (2026-11-14)

The original 74-target CTest matrix has since grown. The latest current
all-target Ninja build completes and full CTest reports **77/77 PASS**.

A process-isolated Qt/V8 QA layout was built and checked using restricted
PATH: standalone Qt Project Manager, Content Browser, Camera Timeline Editor,
and unified Qt Editor all pass. Both source and archived JavaScript lifecycle
tests pass on the isolated Player without missing-asset diagnostics.

The production package remains blocked by unreviewed V8 14.0.210.0,
the unmodified flat deployment contract, and missing independent clean-machine
release acceptance.

See \`docs/V8_QT_PROCESS_ISOLATION_2026-11-14.md\` for branch pin, updated
launcher paths, manifests and full test scope.

## Current functional / build acceptance

- The native V8 player ran lifecycle `start/update/stop` in both source and
  archive modes, including the installed **staging** Player.
- A fresh Release build log contains all 589 Ninja build steps and successful
  linking of both `northstarcpp_player.exe` and `northstarcpp_editor.exe`.
- A following `ninja -j4 all` on the updated source tree exited with code 0.
- All test targets were built.
- **Full current CTest: 74 of 74 passed**, with zero reported failures.
  The test log is `out/scripting-release-clean-20261114/release-full-ctest-latest.log`.

### Nine release test failures resolved

The historical 65/74 result was superseded by a full 74/74 rerun.

- The Editor/Player smoke script was incorrectly looking for text scene data
  when the current engine writes binary `NSCN`. Fixed without changing the
  binary scene format.
- SceneWorld V3 native smoke was rebuilt and now passes with legacy-to-binary
  migration; diagnostic output remains for future failures.
- Workspace UI visual test passes on the latest run.
- Player malformed-CLI error processing no longer opens a blocking modal GUI
  dialog before options are parsed; the logger lifecycle test now passes.
- The three legacy FPS CTests use `tests/fixtures/legacy_fps_project`,
  a **separate regression fixture** with camera configuration, sound clips,
  and its own scene. The modern script-owned FPSArena was not changed.
- Independent ECS provider and provider capability-swap tests pass.

## Blocking V8 security issue

Installed SDK header:

```text
out/v8-sdk-cache-compat/ucrt64/include/v8-version.h
V8 14.0.210.0
```

This is **not attested** as a currently maintained stable V8 build with recent
security backports. The November 11, 2026 Chrome 157 stable security update
lists multiple high severity V8 fixes. The latest SDK must be built from the
tip of V8's currently supported stable branch and pinned to a verifiable
revision with an auditable build record.

Sources:
- https://v8.dev/docs/release-process
- https://chromereleases.googleblog.com/2026/11/stable-channel-update-for-desktop_01744469280.html

Do **not** approve V8 solely because a native smoke or gameplay integration
test passes. An outdated JS engine can be functional while vulnerable.

## Blocking Qt/V8 runtime dependency collision

Current dependency scanner result:

```text
out/scripting-release-clean-20261114/v8-qt-runtime-collision-report.json
safe_to_merge = false
```

Seven shared DLL names have mismatching SHA-256 hashes:

- libgcc_s_seh-1.dll
- libicudt78.dll
- libicuin78.dll
- libicuuc78.dll
- libstdc++-6.dll
- libwinpthread-1.dll
- zlib1.dll

The current V8 library comes from an UCRT64 SDK; the current core Editor/Player
uses a MinGW64 build. A successful test with a manually modified PATH does not
prove one merged deployed DLL directory is safe.

Choose one supported, reproducible toolchain/runtime closure for the final
distribution, rebuild V8 against it, and confirm PE import closures, Qt's
platform plugins, the native V8 provider, and an in-process Player startup
under a **restricted installed-package PATH**. Do not simply overwrite the
seven runtime DLLs with arbitrary copies.

Qt's Windows deployment guide recommends `windeployqt` for Qt-controlled
deployment plus checks for other third-party dependencies:
https://doc.qt.io/qt-6/windows-deployment.html

## Blocking clean-machine release validation

The installed **staging** player source/archive E2E passed. This is not yet a
production installation acceptance in Windows Sandbox or a clean virtual
machine with no development SDK environment present.

Final acceptance requires:

1. Rebuild using a freshly verified stable V8 SDK with security fixes.
2. Resolve the Qt/UCRT/MinGW DLL conflict with a consistent dependency set.
3. Run Ninja all-targets from a new empty build directory and full CTest.
4. Stage the supported SDK/runtime DLL import closure with hashes.
5. Launch Editor and Player and a packaged real game inside a clean Windows
   environment with no compiler, SDK or development PATH dependencies.
6. Verify startup, source/archive scripts, scene rendering, save games,
   physics, teardown, and no missing imports.
7. Record the exact binary hashes, tested host version, compiler/Qt/V8
   versions, licenses, and a verified run manifest before any publish.
8. Replace production DLLs and `dist` **only after** every gate passes,
   with a rollback plan.

## Fail-closed release gate

Run in the workspace:

```powershell
python tools/release_readiness_gate.py `
  --build out/scripting-release-clean-20261114 `
  --sdk out/v8-sdk-cache-compat/ucrt64 `
  --provider out/scripting-v8-ucrt-build/bin/scripting_v8.dll `
  --report out/scripting-release-clean-20261114/release-readiness-gate.json
```

Currently returns exit code **2**, `release_approved=false`. The gate
checks the full clean build log, both binaries, V8 version/provenance,
Qt/V8 mismatch report, the latest CTest, staged E2E and independent
clean-machine acceptance. A V8 security attestation must be independently
verified; it is not created automatically.

No shipping `dist` or production module DLL was overwritten.

## Private Qt production build integration

The production release builder now contains process-separated Qt/V8
staging logic and signed V8 binary approval enforcement. Real installed
QA smoke tests pass in an isolated directory. This is not evidence
of current V8 SDK security nor a clean-machine installation acceptance.
See docs/V8_QT_PROCESS_ISOLATION_2026-11-14.md.
