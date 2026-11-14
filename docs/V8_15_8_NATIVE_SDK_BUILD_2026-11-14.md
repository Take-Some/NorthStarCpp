# NorthStarCpp - V8 15.8 native Windows SDK build

Date: 2026-11-14
Release status: BLOCKED (required build prerequisites and signed security acceptance)
Native SDK status: NOT COMPILED on this workstation
Developer V8 sidecar: functional for local QA only

## What was found

The existing out/v8-official-windows/15.8.62 directory contains an
official V8 d8.exe, its ICU data, an independently compiled ABI 2
scripting_v8_sidecar.dll and prior working Player/JavaScript regression
results. It is NOT an embeddable SDK and does not include v8_monolith.lib
or complete V8 C++ headers. It cannot be silently relabeled as an SDK.

A 15.8 source pin has already been recorded:
- Repository: https://chromium.googlesource.com/v8/v8.git
- Branch: refs/branch-heads/15.8
- Pinned commit: 19c06df29e0050413f5f041888f4c990de970314
- Previous provenance record: out/v8-official-windows/15.8.62/stable-source-pin.json
- The official branch SHA was reverified on 2026-11-14.

## Added source-built SDK pipeline

tools/v8_provider/build_official_sdk.py

The builder verifies the pre-existing official source pin, requires
a clean official V8 checkout, executes fetch v8, checks out the
pinned commit, performs a pinned gclient sync, and uses GN/Ninja
to build BOTH v8_monolith and d8.

Target: Windows x64, clang-cl with MSVC C++ ABI, static COFF monolith.
The configuration explicitly specifies:
- is_debug=false, is_component_build=false
- use_custom_libcxx=false (matches native provider MSVC STL)
- v8_monolithic=true and v8_use_external_startup_data=false
- v8_use_snapshot=true
- v8_enable_pointer_compression=true, v8_enable_sandbox=true
- v8_enable_i18n_support=true
- target_cpu=x64, symbol_level=0

The generated SDK contains public headers, lib/v8_monolith.lib,
bin/d8.exe, available ICU data and original upstream licenses.
northstar-v8-sdk.json records the exact official commit, GN flags,
file sizes and SHA-256 hashes and has release_approved=false.

The builder fails closed on missing toolchains, changed source,
inconsistent versions, missing monolith archives, nonempty output,
or unsafe dist destinations. It never installs into production.

tools/v8_provider/verify_official_sdk.py checks exact files, hashes,
monolithic build configuration and source provenance. It rejects
the existing d8-only archive.

tools/v8_provider/official_sdk/CMakeLists.txt prepares the separate
native scripting_v8.dll provider. The module links the COFF library
with MSVC-compatible clang-cl, exports engine.scripting C ABI 2 and
never exposes V8 C++ classes to MinGW Player/Editor. Its CMake
configuration refuses MinGW, missing SDK files and altered hashes.
Optional ABI smoke runs against the existing NorthStar C ABI test host.

## Commands once the required Windows tools are installed

From the project root, with depot_tools, Visual Studio C++ Build
Tools and Windows SDK installed and available in a Developer shell:

    python tools/v8_provider/build_official_sdk.py ^
      --pin out/v8-official-windows/15.8.62/stable-source-pin.json ^
      --work-dir D:/NorthStarV8/build-15.8 ^
      --sdk-dir D:/NorthStarV8/sdk-15.8 ^
      --mode build ^
      --depot-tools D:/NorthStarV8/depot_tools

Then compile the isolated V8 C ABI 2 provider:

    cmake -S tools/v8_provider/official_sdk ^
      -B D:/NorthStarV8/native-v8-provider -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_COMPILER=clang-cl ^
      -DNORTHSTARCPP_V8_SDK_ROOT=D:/NorthStarV8/sdk-15.8 ^
      -DNORTHSTARCPP_ENGINE_ROOT=%CD% ^
      -DNORTHSTARCPP_HOST_SMOKE_EXE=%CD%/out/qt-editor-clean-verify-20261114/northstarcpp_scripting_smoke.exe ^
      -DNORTHSTARCPP_HOST_ASSETS_DLL=%CD%/out/qt-editor-clean-verify-20261114/assets_directory.dll

    cmake --build D:/NorthStarV8/native-v8-provider --parallel 4
    ctest --test-dir D:/NorthStarV8/native-v8-provider --output-on-failure

Run from cmd.exe or a VS Developer Command Prompt; multiline ^
continuation is CMD syntax.

## Checks performed today

- Official 15.8 branch resolves to the recorded source SHA: PASS.
- Reproducible build-plan generation: PASS.
- New CTest northstarcpp_v8_official_sdk_build_plan: PASS.
- D8-only archive rejected as an SDK: PASS.
- Incompatible MinGW provider toolchain rejected: PASS.
- Build execution preflight on D: (1,578 GiB free): BLOCKED.
- Missing prerequisites: depot_tools fetch/gclient, GN and Visual
  Studio/Windows clang-cl/MSVC C++ toolchain/Windows SDK.
- No native SDK produced, and no native provider was linked.
- No production dist or installed game DLLs were changed.

Current preflight: out/v8-official-windows/sdk-build-preflight.json
Build plan: out/v8-official-windows/official-sdk-build-plan.json

## Immediate local game testing (development only)

tools/v8_provider_sidecar/launch_qa.py and launch_qa_editor.cmd
use the existing official V8 15.8.62 d8 worker through
scripting_v8_sidecar.dll. The launcher verifies the SHA-256 of
provider/worker/d8 and does not modify the installed QA package.
Its --smoke mode passed a Player App startup using a scene with
JavaScript and engine.scripting ABI 2. The launcher is explicitly
development-only; this is not an SDK and does not resolve the signed
release gate.

## Remaining acceptance gates

1. Install official depot_tools, GN and Windows C++ Build Tools/SDK.
2. Perform full pinned source build (not a d8 binary-only download).
3. Verify SDK manifest and compile the native scripting_v8.dll.
4. Pass native JS ABI 2, engine API, source/FAT/DB scene and timeout tests.
5. Audit Qt/V8 runtime, third-party licenses and security patches.
6. Obtain independent security approval and clean-Windows acceptance.
7. Only then consider explicit release package promotion.

Official references: https://v8.dev/docs/source-code,
https://v8.dev/docs/build, https://v8.dev/docs/build-gn,
https://v8.dev/docs/embed

## Follow-up: cold-start deadline regression in development sidecar

A separate regression in the existing d8-based developer provider was
reproduced after the initial SDK pipeline work. The per-script evaluation
deadline (typically 1 second, or 200 ms in some ABI tests) began before
the d8 process reached READY, allowing cold worker startup to exhaust
the execution budget without evaluating any JavaScript.

The sidecar now starts the per-script deadline only AFTER the already
bounded 15-second READY handshake. The native V8 SDK/provider source is
independent of this change.

A separately built development-only DLL was generated at:
out/v8-official-windows/15.8.62/sidecar-startup-fix-build/
scripting_v8_sidecar.dll

Its SHA-256 provenance and explicit non-release classification are in:
out/v8-official-windows/15.8.62/sidecar-startup-fix-provenance.json

Validation results:
- 8/8 repeated Player startup runs with real JavaScript: PASS.
- 6/6 invocations via the QA editor launch .cmd, --smoke: PASS.
- Script service override integration: PASS.
- Full V8 SDK/C ABI conformance: NOT PASS with the developer sidecar
  (missing exact native-error semantics and tight 200 ms test timings).
  These results do NOT certify the sidecar as a replacement native SDK.

The launcher verifies SHA-256 of the new DLL, d8.exe and worker script;
existing published/provenance artifacts have not been overwritten.
Production dist and the required signed V8 native release gates remain
unchanged.
