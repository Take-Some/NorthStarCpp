# NorthStarCpp - V8 SDK vendor download

Date: 2026-11-14
Canonical location: sdk/vendor (third-party dependencies).
NorthStar public API remains in sdk/include/northstarcpp.

## Complete prebuilt V8 SDK downloaded

Path:
sdk/vendor/v8/15.7.112.23-7-windows-x64-msvc/

Source: https://github.com/haroel/v8-build/releases/tag/v15.7.112.23-7
This is an independently produced third-party prebuilt, not a Google-
signed binary distribution. The V8 source tag is 15.7.112.23 at commit
440c1c463e2e528c309af5067952e7b11631507f.

Downloads:
- include-win.zip (365142 bytes)
  SHA256 515e34b3a15ebaac7f0d1e01c043caa0ad5101dd836dcc9819c2e91ff345f196
- libv8_monolith-win-x64-release.zip (46101634 bytes)
  SHA256 afafa6040932063cb21b0c5afce9608287b6980acc078f19d1069eccbb03988e

Installed: 127 hash-verified files, including:
- include/v8.h and include/v8-version.h
- include/libplatform/libplatform.h
- lib/v8_monolith.lib (271369706 bytes, static COFF archive)
- build-config/args.gn and args.full.txt
- LICENSE obtained from the matching official V8 source tag
- vendor-sdk.json with file-level SHA256 and release metadata

CRITICAL: This is V8 15.7, not 15.8. Its GN flags include
v8_enable_pointer_compression=false, v8_enable_sandbox=false,
v8_enable_i18n_support=false. Do not combine it with V8 15.8 headers
or the existing 15.8 engine.scripting native provider configuration.
The sandbox-disabled third-party SDK is local-development-only:
security_reviewed=false, release_approved=false.

## Exact official V8 15.8 source downloaded

Path:
sdk/vendor/sources/v8-15.8.62.38-19c06df2/

Official source commit:
19c06df29e0050413f5f041888f4c990de970314
(Chromium V8 refs/branch-heads/15.8)

Source mirror archive:
https://codeload.github.com/v8/v8/tar.gz/19c06df29e0050413f5f041888f4c990de970314

Archive 32207599 bytes, SHA256:
74901683fc7b3ba035e27a200f2adfe6dd0e5be714f315b19996c5b59b5e9670

20446 source files extracted (~160.5 MB). The version in upstream
include/v8-version.h is 15.8.62.38. The source includes DEPS, LICENSE,
headers and implementation; SOURCE-PROVENANCE.json records provenance.

This is SOURCE ONLY. No matching V8 15.8 monolithic library exists
in that directory. gclient dependency fetch and the official Windows
GN/clang-cl compilation remain required. Do not label the source tree
a finished V8 binary SDK.

## Architecture

sdk/include/northstarcpp/ : stable engine API/ABI, no vendor V8 C++ types
sdk/vendor/v8/ : versioned prebuilt embeddable vendor SDKs
sdk/vendor/sources/ : official upstream source snapshots
engine/northstarcpp/modules/scripting/v8/ : NorthStar provider source
tools/v8_provider/ : V8 acquisition, build and verification automation
out/vendor-download-cache/v8/ : original downloaded archives
dist/ : approved releases only; NOT modified

The two versioned vendor directories are intentionally not auto-selected
for the game. A native V8 provider must match headers, static library,
GN flags and clang-cl/MSVC C++ ABI. Cross-runtime interaction with
NorthStar is solely engine.scripting C ABI 2.

## Verification

python tools/v8_provider/verify_vendor_install.py
python tools/verify_architecture.py

To build the target 15.8 SDK after installing depot_tools, GN and the
Windows C++/clang-cl toolchain, use the pinned build script and separate
D: build workspace. Target destination must be:

sdk/vendor/v8/15.8.62.38-windows-x64-msvc/

The current third-party V8 15.7 SDK is NOT an acceptable replacement
for the reviewed V8 15.8 release build. Production dist and DLLs were
not replaced.
