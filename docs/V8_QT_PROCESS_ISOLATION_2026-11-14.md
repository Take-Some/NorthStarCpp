# NorthStarCpp — Qt/V8 process isolation and stable V8 source lock
Date: 2026-11-14  
Copyright (C) 2026 Kayla's Systems — Kayla Verner

## Release decision: BLOCKED

This document reports an experimental QA **process-isolated** runtime
layout. It is **not** an approved, installable commercial release. The
existing SDK is V8 14.0.210.0 without an independently verified security
patch chain. All production `dist` and installed DLLs remain unchanged.

## Upstream source discovery

The current ChromiumDash Windows Stable release lookup (includes early
Stable) returned Chrome **158.0.8127.37** for milestone 158 and V8 branch
`refs/branch-heads/15.8`. The official V8 Git remote resolved the tip to:

```text
19c06df29e0050413f5f041888f4c990de970314
```

The V8 revision recorded with Chrome 158.0.8127.37 was:

```text
b601a2347f005e8bfadd32f285df97ffba5cf1bd
```

The branch tip differs from the Chromium release revision because the V8
team backports fixes to Stable. **These commit values are a dated
upstream source pin, not an approved binary SDK or proof of security.**
The candidate may change when new stable builds are published. Re-query
upstream and pin a reviewed commit for each production update.

Reproduce:

```powershell
python tools/v8_provider/resolve_stable_source.py `
  --output out/scripting-release-clean-20261114/qa-process-isolated-v3/v8-upstream-source-pin.json
```

Source-of-truth references:

- https://v8.dev/docs/release-process
- https://chromiumdash.appspot.com/fetch_milestones?only_active=true
- https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Windows&num=10
- https://chromium.googlesource.com/v8/v8.git
- https://chromereleases.googleblog.com/2026/11/early-stable-update-for-desktop_0520415684.html

## Runtime ABI findings

The current `northstarcpp_player.exe` and native
`northstarcpp_editor.exe` do not directly import Qt. Qt is imported by
the standalone Project Manager, Content Browser, Camera Timeline Editor
and unified Qt Editor. The V8 runtime uses a UCRT64 toolchain while Qt
currently deploys MinGW64 runtime DLLs.

Seven overlapping DLL basenames had differing SHA-256 values in the
previous *flat* deployment:

```text
libgcc_s_seh-1.dll
libicudt78.dll
libicuin78.dll
libicuuc78.dll
libstdc++-6.dll
libwinpthread-1.dll
zlib1.dll
```

Copying either vendor's versions over the other's in a single process
directory is unsupported. Instead the QA directory contains:

```text
qa-process-isolated-v3/
  bin/
    northstarcpp_player.exe
    northstarcpp_editor.exe
    scripting_v8.dll
    libv8.dll
    libv8_libbase.dll
    libv8_libplatform.dll
    [V8/UCRT DLL dependencies]
    qt_tools/
      northstarcpp_project_manager.exe
      northstarcpp_asset_browser.exe
      northstarcpp_camera_timeline_editor.exe
      northstarcpp_editor_qt.exe
      Qt6*.dll
      [Qt/MinGW64 DLL dependencies]
      platforms/qwindows.dll
```

The native editor's tool launcher and the Qt Project Manager can locate
these executables in the private `qt_tools` directory while retaining
support for the preexisting flat development layout.

## QA execution and results

The separate bin trees were validated by PE recursive-import checks and
actual Windows process tests with a restricted PATH containing only the
Windows system directories.

| Test | Result |
| --- | --- |
| Qt Project Manager `--smoke` | PASS |
| Qt Content Browser `--smoke` | PASS |
| Qt Camera Timeline Editor `--smoke` | PASS |
| Qt unified Editor `--smoke` | PASS |
| V8 source JS `start/update/stop` | PASS |
| V8 archived JS `start/update/stop` | PASS |
| V8 missing-asset warnings | Strictly rejected by new E2E script |
| Qt recursive local PE imports | 0 unresolved |
| Engine/V8 recursive local PE imports | 0 unresolved |
| Updated Ninja build | PASS |
| Full CTest | **77/77 PASS** |
| Security-reviewed V8 SDK | NOT AVAILABLE |
| Independent clean-machine installation | NOT VERIFIED |
| Production flat-package compatibility | NOT RESOLVED; current builder intentionally rejects collisions |

Artifacts (all QA-only):

- `out/scripting-release-clean-20261114/qa-process-isolated-v3/process-isolation-report.json`
- `out/scripting-release-clean-20261114/qa-process-isolated-v3/qa-updated-binaries-checks.json`
- `out/scripting-release-clean-20261114/qa-process-isolated-v3/qa-updated-binaries-e2e.json`
- `out/scripting-release-clean-20261114/qa-process-isolated-v3/v8-upstream-source-pin.json`

The first mixed test fixture had a Scene V3 model reference that its tiny
FAT/DB test archive did not contain. The E2E runner now rejects missing-asset
messages rather than mistaking JS lifecycle success for a fully valid game.
A corrected test fixture with all referenced resources satisfied passed.

## Security and release controls

`tools/release_readiness_gate.py` now requires a **detached Ed25519
signature** verifiable with the trusted release public key located at
`tools/v8_provider/trust/release_approval_ed25519.pub.pem`. The key
must be provisioned and audited by release engineering. No fake key or
unreviewed "approval" is supplied by this task. An unsigned JSON file
containing `approved: true` is not accepted.

The separately created QA staging tool is
`tools/v8_provider/qa_isolate_runtimes.py`. It rejects targets under
`dist` and will not overwrite existing staging directories. Its report
explicitly sets `approved_for_release=false`.

## Remaining production tasks

1. Fetch and verify the complete current V8 Stable source and all
   `gclient` dependencies, build using a supported Windows toolchain and
   package exact headers and libraries. GN/depot_tools and their full
   dependencies were not available for a verified new V8 build in this
   session. The source revision has been resolved, not compiled.
2. Rebuild `scripting_v8.dll` against the new SDK and rerun real ABI 2,
   integration, archive and long-running stress tests.
3. Convert the *actual* `tools/build_dist.py` release package contract to
   the verified two-process-directory layout. The current flat package
   still intentionally rejects the seven mismatching DLL names; do not
   disable this protection.
4. Rebuild from a fresh empty Ninja directory and rerun full CTest,
   with the new V8 runtime and updated launchers.
5. Install the output on a clean Windows VM/Sandbox with all developer
   PATH and SDK dependencies absent. Verify Qt tools, native game,
   rendering, physics, source/archive scripts, logging, save and shutdown.
6. Review dependency licenses and produce signed SHA-256 artifact records
   before promotion. Release gate must return exit 0.
7. Only then stage a controlled replacement of `dist` and production DLLs
   with an atomic rollback path.

**No production package or installed native module was replaced.**


## Production builder integration update

The actual tools/build_dist.py now supports process-private Qt DLL
deployment whenever native V8 is requested; Qt executables and DLLs
are placed in bin/qt_tools, while V8 libraries stay in bin.
Older flat Qt-only builds remain supported.

Production V8 stages require a signed, independently reviewed
security attestation. The builder refuses the currently unsigned
legacy SDK and does not overwrite production dist or native DLLs.

Real QA installed-stage copied to an ephemeral directory:
* Qt manifest relocation and verification: PASS
* Engine plugin registry: 9 registered plugins validated
* Binary imports: 50 binaries inspected without missing imports
* Installed package_smoke(): PASS
* Original source staging directory: unchanged

Regression suites: Qt 7/7, V8 registry 8/8, release isolation 3/3.
Missing V8 attestation is rejected by build_dist with exit code 2.
The full approved V8 15.8 SDK and independent clean Windows VM
installation tests are still outstanding.

## Latest QA build and gating update

The production builder now requires two-phase QA/promote, signed V8
security evidence, and an independently signed clean Windows VM report
before publication. Latest native-only QA package NorthStarCpp 0.4.0
passed 82/82 CTest, installed smoke and a 200-file manifest.
It does not include V8 15.8. Full source build and clean VM acceptance
remain pending. See V8_15_8_CLEAN_VM_RELEASE_GATES_2026-11-14.md.
