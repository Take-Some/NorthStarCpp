# NorthStarCpp — V8 release acceptance audit

Date: **14 November 2026**  
Project: `C:\Users\AidenFox\Documents\xray2`  
Outcome: **engine/player functionality verified; production V8 release held for security and binary-runtime compatibility**.

## Proven acceptance results

| Gate | Result | Evidence |
| --- | --- | --- |
| Fresh Release build, every production Ninja target | PASS | `out/scripting-release-clean-20261114/` |
| Repeated `ninja all` / `ninja -n all` | PASS | No pending build actions before enabling test targets |
| Windows player with original 40-object project, `scripts/main.js` | PASS | `full-clean-player-v8-e2e.json` |
| Windows player with FAT/DB `--archive` + `scripts/main.js` | PASS | Same two-mode E2E report |
| Installed QA player with V8, source and archive | PASS / PASS | `installed-stage-v8-e2e.json` |
| Installed CMake components (Runtime, SDK, Tools, Documentation, Licenses) | PASS | `qa-installed-staging/` |
| Installed Qt dependencies | PASS | 33 copied Qt binaries; `qt.runtime.json` |
| Installed module registry validation | PASS | 9 registered modules |
| PE DLL dependency closure and runtime hashes | PASS for installed QA package | 50 PE import tables audited |
| Installed application smoke | PASS | `package_smoke` on QA staging |
| Installed SDK consumer smoke | PASS | `sdk_smoke` on QA staging |
| Python Qt/V8/game distribution tests | 28 / 28 PASS | `unittest` |
| Native V8 CTest from separate UCRT64 provider build | PASS | `northstarcpp_scripting_real_v8` |
| Release-tree CTest suite (`BUILD_TESTING=ON`) | **Pending final log inspection** | 74 registered tests |
| Patched V8 suitable for production | BLOCKED | MSYS2 UCRT64 package below security gate |
| Safe one-directory Qt/V8 DLL merge | BLOCKED | Seven SHA-256 mismatches |
| Signed/public production `dist` | NOT PERFORMED | No vulnerable V8 deployed |

The successful V8 end-to-end tests use **the existing development V8
14.0.210**, only for trusted local scripts. A successful development runtime
test is not a certificate of safety for public distribution.

## Clean CMake / Ninja repairs

Clean build root: `out/scripting-release-clean-20261114`.
CMake Release configured in a never-before-used build directory (first with
`BUILD_TESTING=OFF`) and compiled all 589 production tasks.

The first all-target verification revealed that `Qt6Concurrent.dll` was absent
from the native Qt Content Browser runtime; the executable's import table
alone did not necessarily retain a direct reference, although the deployment
manifest required the DLL. `tools/deploy_qt.py` now explicitly stages
Qt6Concurrent for the native asset browser and validates its transitive imports.
After re-link, `ninja all` and `ninja -n all` both returned exit code zero
with no remaining build work. Later `BUILD_TESTING=ON` registered 74 CTests
and linked 80 additional test build actions.

This fresh tree replaces the earlier corrupted/interrupted Ninja build state
for verification purposes. No production artifacts were overwritten.

## Qt/V8 binary runtime incompatibility

The build's MINGW64 QtCore imports MSVCRT, `libstdc++-6.dll`,
`libgcc_s_seh-1.dll`, ICU 78, `libwinpthread-1.dll`, and `zlib1.dll`.
The UCRT64 V8 import graph depends on different binaries with identical names.
The incompatibilities are verified by SHA-256; seven conflicting DLLs:

- `libgcc_s_seh-1.dll`
- `libicudt78.dll`
- `libicuin78.dll`
- `libicuuc78.dll`
- `libstdc++-6.dll`
- `libwinpthread-1.dll`
- `zlib1.dll`

Machine-readable report:
`out/scripting-release-clean-20261114/v8-qt-runtime-collision-report.json`.

A Windows in-process loader cannot safely select two different DLLs with
identical names for separate subsystems in the same global process context.
Using different `PATH` values, installing the DLLs in another directory,
or overwriting Qt's copies is not an accepted release fix.

**Required release design choice:** either build a patched V8 against a
toolchain/runtime family compatible with the shipping Qt and NorthStar
binaries, or host the V8 runtime in a separate process with a versioned IPC
contract. Neither production architecture has been delivered yet.

## Security / provenance

MSYS2's official UCRT64 V8 package currently lists version
`14.6.202.26-5` and CVE-2026-85046 (high), fixed from V8 `15.3.48`.
See: https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-v8 .

The SDK extracted for local development reports version `14.0.210` both
in `v8.pc` and from the actual `bin/d8.exe` execution. It fails the
production release gate before writing any packaged files.

`tools/v8_provider/registry.py` now verifies the real `d8` version matches
SDK metadata, requires staged DLL hashes to match that same SDK, and checks
all destination DLL collisions **before copying anything**.

`tools/v8_provider/runtime_audit.py` audits Qt and V8 side by side. The
official release pipeline runs this preflight before attempting V8 registration.
`tools/build_dist.py` additionally prevents reusing a test-free
`BUILD_TESTING=OFF` build for official release and requires at least 10
registered CTests before executing the full test suite.

These controls enforce a minimum reviewed version and binary consistency;
they do **not** replace checking all subsequent V8 security advisories,
third-party licenses, signing and reproducible artifact provenance.

## Next production release conditions

1. Obtain or build a supported, security-reviewed patched V8 SDK for Windows.
2. Resolve the Qt/V8 native DLL conflict without global DLL replacement.
3. Rebuild `scripting_v8.dll` with the selected supported SDK and test the
   actual SDK binary with `d8` and C ABI smoke.
4. Rerun the clean Release build with `BUILD_TESTING=ON`, all 74 CTests,
   Qt deployment validation, PE import closure, both real player E2Es,
   and SDK consumer smoke.
5. Use `tools/build_dist.py` with the tested V8 provider and a fresh
   distribution build. Verify its `manifest.json`, `v8.runtime.json`,
   third-party notices, and packaged games before production replacement.

**No public release, production V8 provider swap, or modification of the
existing `dist` was performed.**
